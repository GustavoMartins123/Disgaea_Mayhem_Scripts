#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../../native/mod_menu_overlay/vendor/minhook/include/MinHook.h"
#include "../../native/mod_loader/mod_loader_api.h"

namespace {

constexpr std::uintptr_t kConsumeItemRva = 0x0082F6C0;
constexpr std::uintptr_t kInventoryServiceGlobalRva = 0x00D3AD88;
constexpr std::uintptr_t kInventoryServiceVtableRva = 0x00A85BE8;
constexpr std::size_t kResultStateOffset = 0x08;
constexpr std::size_t kOperationOffset = 0x10;
constexpr std::size_t kItemDefinitionOffset = 0x14;
constexpr std::int32_t kConsumeOperation = 2;
constexpr std::int32_t kOperationSucceeded = 1;
constexpr DWORD kExpectedTimestamp = 0x6A6AB373;
constexpr DWORD kExpectedImageSize = 0x00E01000;

using ConsumeItemFn = bool (*)(void* service, const char* item_id,
                               std::uint32_t quantity);

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_shutting_down{false};
std::atomic<bool> g_validation_error_logged{false};
std::atomic<LONG> g_active_calls{0};
std::uintptr_t g_exe_base = 0;
void* g_hook_target = nullptr;
ConsumeItemFn g_original_consume_item = nullptr;
bool g_minhook_initialized = false;
const DmModLoaderApi* g_loader = nullptr;

class HookScope {
public:
    HookScope() { g_active_calls.fetch_add(1, std::memory_order_acq_rel); }
    ~HookScope() { g_active_calls.fetch_sub(1, std::memory_order_acq_rel); }
};

void Log(const char* message) {
    if (g_loader != nullptr && g_loader->Log != nullptr) {
        g_loader->Log("dlc_unlocker", message);
    }
}

bool IsAccessibleRange(const void* address, std::size_t size, bool require_write) {
    if (address == nullptr || size == 0) return false;
    MEMORY_BASIC_INFORMATION information = {};
    if (VirtualQuery(address, &information, sizeof(information)) != sizeof(information) ||
        information.State != MEM_COMMIT ||
        (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }

    const DWORD protection = information.Protect & 0xFF;
    if (require_write && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
        protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) {
        return false;
    }
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    const auto end = reinterpret_cast<std::uintptr_t>(information.BaseAddress) +
                     information.RegionSize;
    return start <= end && size <= end - start;
}

bool ValidateExecutableBuild() {
    if (g_exe_base == 0 ||
        !IsAccessibleRange(reinterpret_cast<void*>(g_exe_base), sizeof(IMAGE_DOS_HEADER), false)) {
        return false;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_exe_base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;

    const auto nt_address = g_exe_base + static_cast<std::uintptr_t>(dos->e_lfanew);
    if (!IsAccessibleRange(reinterpret_cast<void*>(nt_address), sizeof(IMAGE_NT_HEADERS64), false)) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(nt_address);
    return nt->Signature == IMAGE_NT_SIGNATURE &&
           nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
           nt->FileHeader.TimeDateStamp == kExpectedTimestamp &&
           nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
           nt->OptionalHeader.SizeOfImage == kExpectedImageSize;
}

bool EqualsItemId(const char* value, const char* expected) {
    if (value == nullptr) return false;
    const std::size_t expected_length = std::strlen(expected);
    return IsAccessibleRange(value, expected_length + 1, false) &&
           std::memcmp(value, expected, expected_length + 1) == 0;
}

bool ResolveReusableConsumable(const char* item_id, std::uint32_t& definition_id) {
    static const char* const definitions[] = {"1", "2", "3", "4", "5"};
    for (std::uint32_t index = 0; index < 5; ++index) {
        if (EqualsItemId(item_id, definitions[index])) {
            definition_id = index + 1;
            return true;
        }
    }
    return false;
}

bool ResolveInventoryService(void* service) {
    if (!IsAccessibleRange(service, kItemDefinitionOffset + sizeof(std::uint32_t), true)) {
        return false;
    }
    const auto service_address = reinterpret_cast<std::uintptr_t>(service);
    if (*reinterpret_cast<const std::uintptr_t*>(service_address) !=
        g_exe_base + kInventoryServiceVtableRva) {
        return false;
    }

    const auto global_address = g_exe_base + kInventoryServiceGlobalRva;
    if (!IsAccessibleRange(reinterpret_cast<const void*>(global_address),
                           sizeof(std::uintptr_t), false)) {
        return false;
    }
    return *reinterpret_cast<const std::uintptr_t*>(global_address) == service_address;
}

bool HookConsumeItem(void* service, const char* item_id, std::uint32_t quantity) {
    HookScope scope;
    std::uint32_t definition_id = 0;
    if (!g_enabled.load(std::memory_order_acquire) ||
        !ResolveReusableConsumable(item_id, definition_id)) {
        return g_original_consume_item(service, item_id, quantity);
    }

    if (quantity != 1 || !ResolveInventoryService(service)) {
        if (!g_validation_error_logged.exchange(true, std::memory_order_acq_rel)) {
            Log("Consumo rejeitado: pedido ou servico de inventario nao corresponde.");
        }
        return false;
    }

    const auto service_address = reinterpret_cast<std::uintptr_t>(service);
    *reinterpret_cast<std::int32_t*>(service_address + kOperationOffset) = kConsumeOperation;
    *reinterpret_cast<std::uint32_t*>(service_address + kItemDefinitionOffset) = definition_id;
    InterlockedExchange(reinterpret_cast<volatile LONG*>(service_address + kResultStateOffset),
                        kOperationSucceeded);
    return true;
}

bool InstallHook() {
    static const std::uint8_t expected_prologue[] = {
        0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41,
        0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xE9
    };
    g_hook_target = reinterpret_cast<void*>(g_exe_base + kConsumeItemRva);
    if (!IsAccessibleRange(g_hook_target, sizeof(expected_prologue), false) ||
        std::memcmp(g_hook_target, expected_prologue, sizeof(expected_prologue)) != 0) {
        Log("Build rejeitada: rotina de consumo do inventario nao corresponde.");
        g_hook_target = nullptr;
        return false;
    }

    if (MH_Initialize() != MH_OK) {
        Log("Falha ao inicializar MinHook.");
        g_hook_target = nullptr;
        return false;
    }
    g_minhook_initialized = true;
    if (MH_CreateHook(g_hook_target, reinterpret_cast<LPVOID>(&HookConsumeItem),
                      reinterpret_cast<LPVOID*>(&g_original_consume_item)) != MH_OK ||
        MH_EnableHook(g_hook_target) != MH_OK) {
        MH_DisableHook(g_hook_target);
        MH_RemoveHook(g_hook_target);
        MH_Uninitialize();
        g_minhook_initialized = false;
        g_hook_target = nullptr;
        g_original_consume_item = nullptr;
        Log("Falha ao instalar o hook de consumo do inventario.");
        return false;
    }
    return true;
}

void RemoveHook() {
    g_enabled.store(false, std::memory_order_release);
    g_shutting_down.store(true, std::memory_order_release);
    if (g_minhook_initialized && g_hook_target != nullptr) {
        MH_DisableHook(g_hook_target);
        while (g_active_calls.load(std::memory_order_acquire) != 0) Sleep(0);
        MH_RemoveHook(g_hook_target);
    }
    if (g_minhook_initialized) MH_Uninitialize();
    g_minhook_initialized = false;
    g_hook_target = nullptr;
    g_original_consume_item = nullptr;
}

}  // namespace

extern "C" __declspec(dllexport) std::uint32_t WINAPI Mod_GetAbiVersion() {
    return DM_MOD_LOADER_ABI_VERSION;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Initialize(const DmModHostContext* context) {
    if (context == nullptr || context->struct_size != sizeof(DmModHostContext) ||
        context->abi_version != DM_MOD_LOADER_ABI_VERSION || context->loader == nullptr ||
        context->loader->struct_size != sizeof(DmModLoaderApi) ||
        context->loader->abi_version != DM_MOD_LOADER_ABI_VERSION) {
        return FALSE;
    }
    g_loader = context->loader;
    g_exe_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!ValidateExecutableBuild()) {
        Log("Build do jogo rejeitada pelo fingerprint PE x64 esperado.");
        return FALSE;
    }
    g_shutting_down.store(false, std::memory_order_release);
    g_validation_error_logged.store(false, std::memory_order_release);
    return InstallHook() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Enable() {
    if (!g_minhook_initialized || g_hook_target == nullptr) {
        return FALSE;
    }
    g_validation_error_logged.store(false, std::memory_order_release);
    g_enabled.store(true, std::memory_order_release);
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Disable() {
    g_enabled.store(false, std::memory_order_release);
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_SetOption(const char*, const DmModValue*) {
    return FALSE;
}

extern "C" __declspec(dllexport) void WINAPI Mod_Shutdown() {
    RemoveHook();
    g_loader = nullptr;
    g_exe_base = 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
