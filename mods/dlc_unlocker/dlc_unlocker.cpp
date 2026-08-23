#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../../native/mod_menu_overlay/vendor/minhook/include/MinHook.h"
#include "../../native/mod_loader/dm_mod_common.h"

namespace {

constexpr std::uintptr_t kConsumeItemRva = 0x0082F6C0;
constexpr std::uintptr_t kInventoryServiceGlobalRva = 0x00D3AD88;
constexpr std::uintptr_t kInventoryServiceVtableRva = 0x00A85BE8;
constexpr std::size_t kResultStateOffset = 0x08;
constexpr std::size_t kOperationOffset = 0x10;
constexpr std::size_t kItemDefinitionOffset = 0x14;
constexpr std::int32_t kConsumeOperation = 2;
constexpr std::int32_t kOperationSucceeded = 1;

using ConsumeItemFn = bool (*)(void* service, const char* item_id,
                               std::uint32_t quantity);

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_shutting_down{false};
std::atomic<bool> g_validation_error_logged{false};
std::atomic<LONG> g_active_calls{0};
void* g_hook_target = nullptr;
ConsumeItemFn g_original_consume_item = nullptr;
bool g_minhook_initialized = false;
dm::HostLog Log;

bool EqualsItemId(const char* value, const char* expected) {
    if (value == nullptr) return false;
    const std::size_t expected_length = std::strlen(expected);
    return dm::IsReadableRange(value, expected_length + 1) &&
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
    if (!dm::IsWritableRange(service, kItemDefinitionOffset + sizeof(std::uint32_t))) {
        return false;
    }
    const auto service_address = reinterpret_cast<std::uintptr_t>(service);
    if (!dm::HasVtable(service_address, kInventoryServiceVtableRva)) return false;

    const auto global_address = dm::Rva(kInventoryServiceGlobalRva);
    if (!dm::IsReadableRange(reinterpret_cast<const void*>(global_address),
                             sizeof(std::uintptr_t))) {
        return false;
    }
    return *reinterpret_cast<const std::uintptr_t*>(global_address) == service_address;
}

bool HookConsumeItem(void* service, const char* item_id, std::uint32_t quantity) {
    dm::CallGuard scope(g_active_calls);
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
    g_hook_target = reinterpret_cast<void*>(dm::Rva(kConsumeItemRva));
    if (!dm::MatchesPrologue(dm::Rva(kConsumeItemRva), expected_prologue,
                             sizeof(expected_prologue))) {
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
                      reinterpret_cast<LPVOID*>(&g_original_consume_item)) != MH_OK) {
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
        dm::DrainActiveCalls(g_active_calls);
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
    if (!dm::AcceptHostContext(context, true)) return FALSE;
    Log.Bind(context->loader, "dlc_unlocker");
    g_shutting_down.store(false, std::memory_order_release);
    g_validation_error_logged.store(false, std::memory_order_release);
    return InstallHook() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Enable() {
    if (!g_minhook_initialized || g_hook_target == nullptr) {
        return FALSE;
    }
    const MH_STATUS status = MH_EnableHook(g_hook_target);
    if (status != MH_OK && status != MH_ERROR_ENABLED) {
        Log("Falha ao ativar o hook de consumo do inventario.");
        return FALSE;
    }
    g_validation_error_logged.store(false, std::memory_order_release);
    g_enabled.store(true, std::memory_order_release);
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Disable() {
    g_enabled.store(false, std::memory_order_release);
    if (g_minhook_initialized && g_hook_target != nullptr) MH_DisableHook(g_hook_target);
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_SetOption(const char*, const DmModValue*) {
    return FALSE;
}

extern "C" __declspec(dllexport) void WINAPI Mod_Shutdown() {
    RemoveHook();
    Log.Reset();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
