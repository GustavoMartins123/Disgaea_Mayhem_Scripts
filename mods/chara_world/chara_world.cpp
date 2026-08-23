#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "../../native/mod_menu_overlay/vendor/minhook/include/MinHook.h"
#include "../../native/mod_loader/mod_loader_api.h"

namespace {

constexpr std::uintptr_t kTurnResolveRva = 0x00461CA0;
constexpr std::uintptr_t kCharacterWorldInformationVtableRva = 0x00A57610;
constexpr std::uintptr_t kEnergyValueVtableRva = 0x00A1A7B0;
constexpr std::size_t kInformationPointerOffset = 0x200;
constexpr std::size_t kEnergyValueOffset = 0x170;
constexpr std::size_t kCurrentEnergyOffset = 0x178;
constexpr DWORD kExpectedTimestamp = 0x6A6AB373;
constexpr DWORD kExpectedImageSize = 0x00E01000;

using TurnResolveFn = bool (*)(void* task);

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_freeze_energy{true};
std::atomic<std::int32_t> g_target_energy{100};
std::atomic<LONG> g_active_calls{0};
std::atomic<bool> g_shutting_down{false};

std::uintptr_t g_exe_base = 0;
void* g_hook_target = nullptr;
TurnResolveFn g_original_turn_resolve = nullptr;
bool g_minhook_initialized = false;
const DmModLoaderApi* g_loader = nullptr;

class HookScope {
public:
    HookScope() { g_active_calls.fetch_add(1, std::memory_order_acq_rel); }
    ~HookScope() { g_active_calls.fetch_sub(1, std::memory_order_acq_rel); }
};

void Log(const char* message) {
    if (g_loader != nullptr && g_loader->Log != nullptr) {
        g_loader->Log("chara_world", message);
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

std::int32_t* ResolveCurrentEnergy(void* task) {
    if (!IsAccessibleRange(task, kInformationPointerOffset + sizeof(void*), false)) return nullptr;

    const auto task_address = reinterpret_cast<std::uintptr_t>(task);
    const auto information = *reinterpret_cast<std::uintptr_t*>(
        task_address + kInformationPointerOffset);
    if (!IsAccessibleRange(reinterpret_cast<void*>(information),
                           kCurrentEnergyOffset + sizeof(std::int32_t), true)) {
        return nullptr;
    }

    if (*reinterpret_cast<std::uintptr_t*>(information) !=
            g_exe_base + kCharacterWorldInformationVtableRva ||
        *reinterpret_cast<std::uintptr_t*>(information + kEnergyValueOffset) !=
            g_exe_base + kEnergyValueVtableRva) {
        return nullptr;
    }

    return reinterpret_cast<std::int32_t*>(information + kCurrentEnergyOffset);
}

void ApplyConfiguredEnergy(void* task) {
    if (!g_enabled.load(std::memory_order_acquire) ||
        !g_freeze_energy.load(std::memory_order_acquire)) {
        return;
    }

    std::int32_t* energy = ResolveCurrentEnergy(task);
    if (energy != nullptr) {
        *energy = g_target_energy.load(std::memory_order_relaxed);
    }
}

bool HookTurnResolve(void* task) {
    HookScope scope;
    ApplyConfiguredEnergy(task);
    const bool result = g_original_turn_resolve(task);
    if (!g_shutting_down.load(std::memory_order_acquire)) {
        ApplyConfiguredEnergy(task);
    }
    return result;
}

bool InstallHook() {
    static const std::uint8_t expected_prologue[] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48,
        0x89, 0x70, 0x18, 0x55, 0x57, 0x41, 0x54, 0x41
    };

    g_hook_target = reinterpret_cast<void*>(g_exe_base + kTurnResolveRva);
    if (!IsAccessibleRange(g_hook_target, sizeof(expected_prologue), false) ||
        std::memcmp(g_hook_target, expected_prologue, sizeof(expected_prologue)) != 0) {
        Log("Build rejeitada: prologo da rotina de resolucao de turno nao corresponde.");
        g_hook_target = nullptr;
        return false;
    }

    if (MH_Initialize() != MH_OK) {
        Log("Falha ao inicializar MinHook.");
        g_hook_target = nullptr;
        return false;
    }
    g_minhook_initialized = true;

    if (MH_CreateHook(g_hook_target, reinterpret_cast<LPVOID>(&HookTurnResolve),
                      reinterpret_cast<LPVOID*>(&g_original_turn_resolve)) != MH_OK ||
        MH_EnableHook(g_hook_target) != MH_OK) {
        MH_RemoveHook(g_hook_target);
        MH_Uninitialize();
        g_minhook_initialized = false;
        g_hook_target = nullptr;
        g_original_turn_resolve = nullptr;
        Log("Falha ao instalar o hook de resolucao de turno.");
        return false;
    }

    return true;
}

void RemoveHook() {
    g_enabled.store(false, std::memory_order_release);
    g_shutting_down.store(true, std::memory_order_release);

    if (g_minhook_initialized && g_hook_target != nullptr) {
        MH_DisableHook(g_hook_target);
        while (g_active_calls.load(std::memory_order_acquire) != 0) {
            Sleep(0);
        }
        MH_RemoveHook(g_hook_target);
    }
    if (g_minhook_initialized) MH_Uninitialize();

    g_minhook_initialized = false;
    g_hook_target = nullptr;
    g_original_turn_resolve = nullptr;
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
    return InstallHook() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Enable() {
    if (!g_minhook_initialized || g_hook_target == nullptr) return FALSE;
    g_enabled.store(true, std::memory_order_release);
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Disable() {
    g_enabled.store(false, std::memory_order_release);
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_SetOption(
    const char* key,
    const DmModValue* value) {
    if (key == nullptr || value == nullptr || value->struct_size != sizeof(DmModValue)) {
        return FALSE;
    }

    if (std::strcmp(key, "locked_energy") == 0) {
        if (value->type != DmOptionType::SliderInt ||
            value->int_value < 10 || value->int_value > 100) {
            return FALSE;
        }
        g_target_energy.store(value->int_value, std::memory_order_release);
        return TRUE;
    }

    if (std::strcmp(key, "freeze_energy") == 0) {
        if (value->type != DmOptionType::Toggle) return FALSE;
        g_freeze_energy.store(value->bool_value != FALSE, std::memory_order_release);
        return TRUE;
    }

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
