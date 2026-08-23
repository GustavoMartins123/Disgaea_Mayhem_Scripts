#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "../../native/mod_loader/mod_loader_api.h"
#include "../../native/mod_menu_overlay/vendor/minhook/include/MinHook.h"

namespace {

constexpr std::uintptr_t kApplyRewardsRva = 0x001D77E0;
constexpr std::uintptr_t kItemWorldVtableRva = 0x00A251F0;
constexpr std::size_t kLevelProgressOffset = 0x68;
constexpr DWORD kExpectedTimestamp = 0x6A6AB373;
constexpr DWORD kExpectedSizeOfImage = 0x00E01000;
constexpr LONG kMultiplierScale = 1000;

using ApplyRewardsFn = void (*)(void* item_world, void* result_context);

std::uintptr_t g_exe_base = 0;
void* g_hook_target = nullptr;
ApplyRewardsFn g_original_apply_rewards = nullptr;
bool g_minhook_initialized = false;
volatile LONG g_enabled = FALSE;
volatile LONG g_multiplier_scaled = 5000;
volatile LONG g_invalid_object_logged = FALSE;
SRWLOCK g_reward_lock = SRWLOCK_INIT;
void(WINAPI* g_log)(const char*, const char*) = nullptr;

bool IsReadableRange(std::uintptr_t address, std::size_t size) {
    if (address < 0x10000 || size == 0 || address > std::numeric_limits<std::uintptr_t>::max() - size) {
        return false;
    }
    MEMORY_BASIC_INFORMATION info = {};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) == 0) return false;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
    const DWORD protection = info.Protect & 0xFF;
    const bool readable = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
                          protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
                          protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    if (!readable) return false;
    const auto region_end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    return address + size <= region_end;
}

bool IsWritableRange(std::uintptr_t address, std::size_t size) {
    if (!IsReadableRange(address, size)) return false;
    MEMORY_BASIC_INFORMATION info = {};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) == 0) return false;
    const DWORD protection = info.Protect & 0xFF;
    return protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
           protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

bool ValidateExecutableFingerprint(HMODULE module) {
    if (module == nullptr) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    if (!IsReadableRange(base, sizeof(IMAGE_DOS_HEADER))) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
    const auto nt_address = base + static_cast<std::uintptr_t>(dos->e_lfanew);
    if (!IsReadableRange(nt_address, sizeof(IMAGE_NT_HEADERS64))) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(nt_address);
    return nt->Signature == IMAGE_NT_SIGNATURE &&
           nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
           nt->FileHeader.TimeDateStamp == kExpectedTimestamp &&
           nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
           nt->OptionalHeader.SizeOfImage == kExpectedSizeOfImage;
}

void LogInvalidObjectOnce() {
    if (InterlockedCompareExchange(&g_invalid_object_logged, TRUE, FALSE) == FALSE && g_log != nullptr) {
        g_log("item_world", "Recompensa ignorada: objeto CItemWorldData invalido para esta build.");
    }
}

void HookApplyRewards(void* item_world, void* result_context) {
    if (g_original_apply_rewards == nullptr) return;
    if (InterlockedCompareExchange(&g_enabled, FALSE, FALSE) == FALSE) {
        g_original_apply_rewards(item_world, result_context);
        return;
    }

    const auto object = reinterpret_cast<std::uintptr_t>(item_world);
    const auto progress_address = object + kLevelProgressOffset;
    if (!IsReadableRange(object, sizeof(std::uintptr_t)) ||
        !IsWritableRange(progress_address, sizeof(std::int32_t)) ||
        *reinterpret_cast<const std::uintptr_t*>(object) != g_exe_base + kItemWorldVtableRva) {
        LogInvalidObjectOnce();
        g_original_apply_rewards(item_world, result_context);
        return;
    }

    AcquireSRWLockExclusive(&g_reward_lock);
    auto* progress = reinterpret_cast<std::int32_t*>(progress_address);
    const std::int32_t original_points = *progress;
    const LONG multiplier = InterlockedCompareExchange(&g_multiplier_scaled, 0, 0);

    if (original_points >= 0 && multiplier >= kMultiplierScale) {
        std::int64_t scaled = static_cast<std::int64_t>(original_points) * multiplier;
        scaled = (scaled + (kMultiplierScale / 2)) / kMultiplierScale;
        if (scaled > std::numeric_limits<std::int32_t>::max()) {
            scaled = std::numeric_limits<std::int32_t>::max();
        }
        *progress = static_cast<std::int32_t>(scaled);
        g_original_apply_rewards(item_world, result_context);
        *progress = original_points;
    } else {
        g_original_apply_rewards(item_world, result_context);
    }
    ReleaseSRWLockExclusive(&g_reward_lock);
}

bool InstallHook() {
    static const std::uint8_t expected_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57,
        0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30
    };

    g_hook_target = reinterpret_cast<void*>(g_exe_base + kApplyRewardsRva);
    if (!IsReadableRange(reinterpret_cast<std::uintptr_t>(g_hook_target), sizeof(expected_prologue)) ||
        std::memcmp(g_hook_target, expected_prologue, sizeof(expected_prologue)) != 0) {
        g_hook_target = nullptr;
        return false;
    }

    if (MH_Initialize() != MH_OK) return false;
    g_minhook_initialized = true;
    if (MH_CreateHook(g_hook_target, reinterpret_cast<LPVOID>(&HookApplyRewards),
                      reinterpret_cast<LPVOID*>(&g_original_apply_rewards)) != MH_OK) {
        MH_Uninitialize();
        g_minhook_initialized = false;
        g_hook_target = nullptr;
        return false;
    }
    if (MH_EnableHook(g_hook_target) != MH_OK) {
        MH_RemoveHook(g_hook_target);
        MH_Uninitialize();
        g_original_apply_rewards = nullptr;
        g_minhook_initialized = false;
        g_hook_target = nullptr;
        return false;
    }
    return true;
}

void RemoveHook() {
    if (!g_minhook_initialized) return;
    if (g_hook_target != nullptr) {
        MH_DisableHook(g_hook_target);
        MH_RemoveHook(g_hook_target);
    }
    MH_Uninitialize();
    g_original_apply_rewards = nullptr;
    g_hook_target = nullptr;
    g_minhook_initialized = false;
}

}  // namespace

extern "C" {

__declspec(dllexport) std::uint32_t WINAPI Mod_GetAbiVersion() {
    return DM_MOD_LOADER_ABI_VERSION;
}

__declspec(dllexport) BOOL WINAPI Mod_Initialize(const DmModHostContext* context) {
    if (context == nullptr || context->struct_size != sizeof(DmModHostContext) ||
        context->abi_version != DM_MOD_LOADER_ABI_VERSION || context->loader == nullptr) {
        return FALSE;
    }
    g_log = context->loader->Log;
    const HMODULE executable = GetModuleHandleW(nullptr);
    if (!ValidateExecutableFingerprint(executable)) {
        if (g_log != nullptr) g_log("item_world", "Build do jogo rejeitada pelo fingerprint PE.");
        return FALSE;
    }
    g_exe_base = reinterpret_cast<std::uintptr_t>(executable);
    if (!InstallHook()) {
        if (g_log != nullptr) g_log("item_world", "Falha ao validar ou instalar o hook de recompensas.");
        g_exe_base = 0;
        return FALSE;
    }
    return TRUE;
}

__declspec(dllexport) BOOL WINAPI Mod_Enable() {
    InterlockedExchange(&g_enabled, TRUE);
    return TRUE;
}

__declspec(dllexport) BOOL WINAPI Mod_Disable() {
    InterlockedExchange(&g_enabled, FALSE);
    return TRUE;
}

__declspec(dllexport) BOOL WINAPI Mod_SetOption(const char* key, const DmModValue* value) {
    if (key == nullptr || value == nullptr || value->struct_size != sizeof(DmModValue)) return FALSE;
    if (std::strcmp(key, "level_exp_multiplier") != 0 || value->type != DmOptionType::SliderFloat ||
        !std::isfinite(value->float_value) || value->float_value < 1.0F || value->float_value > 20.0F) {
        return FALSE;
    }
    const LONG scaled = static_cast<LONG>(std::lround(value->float_value * kMultiplierScale));
    InterlockedExchange(&g_multiplier_scaled, scaled);
    return TRUE;
}

__declspec(dllexport) void WINAPI Mod_Shutdown() {
    InterlockedExchange(&g_enabled, FALSE);
    RemoveHook();
    g_exe_base = 0;
    g_log = nullptr;
}

}  // extern "C"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
