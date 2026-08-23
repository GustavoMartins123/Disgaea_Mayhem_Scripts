#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "../../native/mod_loader/mod_loader_api.h"
#include "../../native/mod_menu_overlay/vendor/minhook/include/MinHook.h"

namespace {

constexpr std::uintptr_t kApplyRewardsRva = 0x001D77E0;
constexpr std::uintptr_t kAccumulateItemPointsRva = 0x001D7BD0;
constexpr std::uintptr_t kItemWorldVtableRva = 0x00A251F0;
constexpr std::size_t kLevelProgressOffset = 0x68;
constexpr DWORD kExpectedTimestamp = 0x6A6AB373;
constexpr DWORD kExpectedSizeOfImage = 0x00E01000;
constexpr LONG kMultiplierScale = 1000;

using ApplyRewardsFn = void (*)(void* item_world, void* result_context);
using AccumulateItemPointsFn = void (*)(void* item_world, std::int64_t base_points);

std::uintptr_t g_exe_base = 0;
void* g_apply_rewards_target = nullptr;
void* g_accumulate_item_points_target = nullptr;
ApplyRewardsFn g_original_apply_rewards = nullptr;
AccumulateItemPointsFn g_original_accumulate_item_points = nullptr;
bool g_minhook_initialized = false;
volatile LONG g_enabled = FALSE;
volatile LONG g_level_multiplier_enabled = TRUE;
volatile LONG g_item_point_multiplier_enabled = TRUE;
volatile LONG g_level_multiplier_scaled = kMultiplierScale;
volatile LONG g_item_point_multiplier_scaled = kMultiplierScale;
volatile LONG g_invalid_object_logged = FALSE;
std::atomic<LONG> g_active_calls{0};
std::atomic<bool> g_shutting_down{false};
SRWLOCK g_reward_lock = SRWLOCK_INIT;
void(WINAPI* g_log)(const char*, const char*) = nullptr;

class HookScope {
public:
    HookScope() { g_active_calls.fetch_add(1, std::memory_order_acq_rel); }
    ~HookScope() { g_active_calls.fetch_sub(1, std::memory_order_acq_rel); }
};

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

bool IsExpectedItemWorldObject(void* item_world) {
    const auto object = reinterpret_cast<std::uintptr_t>(item_world);
    return IsReadableRange(object, sizeof(std::uintptr_t)) &&
           *reinterpret_cast<const std::uintptr_t*>(object) ==
               g_exe_base + kItemWorldVtableRva;
}

std::int64_t ScalePositiveValue(std::int64_t value, LONG multiplier) {
    if (value <= 0 || multiplier <= kMultiplierScale) return value;

    const std::int64_t quotient = value / kMultiplierScale;
    const std::int64_t remainder = value % kMultiplierScale;
    const std::int64_t extra =
        (remainder * static_cast<std::int64_t>(multiplier) + (kMultiplierScale / 2)) /
        kMultiplierScale;
    if (quotient > (std::numeric_limits<std::int64_t>::max() - extra) / multiplier) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return quotient * multiplier + extra;
}

void HookApplyRewards(void* item_world, void* result_context) {
    HookScope scope;
    if (g_original_apply_rewards == nullptr) return;
    if (InterlockedCompareExchange(&g_enabled, FALSE, FALSE) == FALSE ||
        InterlockedCompareExchange(&g_level_multiplier_enabled, FALSE, FALSE) == FALSE) {
        g_original_apply_rewards(item_world, result_context);
        return;
    }

    const auto object = reinterpret_cast<std::uintptr_t>(item_world);
    const auto progress_address = object + kLevelProgressOffset;
    if (g_shutting_down.load(std::memory_order_acquire)) {
        g_original_apply_rewards(item_world, result_context);
        return;
    }
    if (!IsExpectedItemWorldObject(item_world) ||
        !IsWritableRange(progress_address, sizeof(std::int32_t))) {
        LogInvalidObjectOnce();
        g_original_apply_rewards(item_world, result_context);
        return;
    }

    AcquireSRWLockExclusive(&g_reward_lock);
    auto* progress = reinterpret_cast<std::int32_t*>(progress_address);
    const std::int32_t original_points = *progress;
    const LONG multiplier = InterlockedCompareExchange(&g_level_multiplier_scaled, 0, 0);

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

void HookAccumulateItemPoints(void* item_world, std::int64_t base_points) {
    HookScope scope;
    if (g_original_accumulate_item_points == nullptr) return;
    if (InterlockedCompareExchange(&g_enabled, FALSE, FALSE) == FALSE ||
        InterlockedCompareExchange(&g_item_point_multiplier_enabled, FALSE, FALSE) == FALSE ||
        g_shutting_down.load(std::memory_order_acquire)) {
        g_original_accumulate_item_points(item_world, base_points);
        return;
    }

    if (!IsExpectedItemWorldObject(item_world)) {
        LogInvalidObjectOnce();
        g_original_accumulate_item_points(item_world, base_points);
        return;
    }

    const LONG multiplier =
        InterlockedCompareExchange(&g_item_point_multiplier_scaled, 0, 0);
    g_original_accumulate_item_points(item_world,
                                     ScalePositiveValue(base_points, multiplier));
}

bool InstallHooks() {
    static const std::uint8_t expected_apply_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57,
        0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30
    };
    static const std::uint8_t expected_item_points_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
        0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41
    };

    g_apply_rewards_target = reinterpret_cast<void*>(g_exe_base + kApplyRewardsRva);
    g_accumulate_item_points_target =
        reinterpret_cast<void*>(g_exe_base + kAccumulateItemPointsRva);
    if (!IsReadableRange(reinterpret_cast<std::uintptr_t>(g_apply_rewards_target),
                         sizeof(expected_apply_prologue)) ||
        std::memcmp(g_apply_rewards_target, expected_apply_prologue,
                    sizeof(expected_apply_prologue)) != 0 ||
        !IsReadableRange(reinterpret_cast<std::uintptr_t>(g_accumulate_item_points_target),
                         sizeof(expected_item_points_prologue)) ||
        std::memcmp(g_accumulate_item_points_target, expected_item_points_prologue,
                    sizeof(expected_item_points_prologue)) != 0) {
        g_apply_rewards_target = nullptr;
        g_accumulate_item_points_target = nullptr;
        return false;
    }

    if (MH_Initialize() != MH_OK) return false;
    g_minhook_initialized = true;
    if (MH_CreateHook(g_apply_rewards_target, reinterpret_cast<LPVOID>(&HookApplyRewards),
                      reinterpret_cast<LPVOID*>(&g_original_apply_rewards)) != MH_OK ||
        MH_CreateHook(g_accumulate_item_points_target,
                      reinterpret_cast<LPVOID>(&HookAccumulateItemPoints),
                      reinterpret_cast<LPVOID*>(&g_original_accumulate_item_points)) != MH_OK ||
        MH_EnableHook(g_apply_rewards_target) != MH_OK ||
        MH_EnableHook(g_accumulate_item_points_target) != MH_OK) {
        MH_DisableHook(g_apply_rewards_target);
        MH_DisableHook(g_accumulate_item_points_target);
        MH_RemoveHook(g_apply_rewards_target);
        MH_RemoveHook(g_accumulate_item_points_target);
        MH_Uninitialize();
        g_original_apply_rewards = nullptr;
        g_original_accumulate_item_points = nullptr;
        g_minhook_initialized = false;
        g_apply_rewards_target = nullptr;
        g_accumulate_item_points_target = nullptr;
        return false;
    }
    return true;
}

void RemoveHooks() {
    if (!g_minhook_initialized) return;
    g_shutting_down.store(true, std::memory_order_release);
    if (g_apply_rewards_target != nullptr) MH_DisableHook(g_apply_rewards_target);
    if (g_accumulate_item_points_target != nullptr) {
        MH_DisableHook(g_accumulate_item_points_target);
    }
    while (g_active_calls.load(std::memory_order_acquire) != 0) Sleep(0);
    if (g_apply_rewards_target != nullptr) MH_RemoveHook(g_apply_rewards_target);
    if (g_accumulate_item_points_target != nullptr) {
        MH_RemoveHook(g_accumulate_item_points_target);
    }
    MH_Uninitialize();
    g_original_apply_rewards = nullptr;
    g_original_accumulate_item_points = nullptr;
    g_apply_rewards_target = nullptr;
    g_accumulate_item_points_target = nullptr;
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
    g_shutting_down.store(false, std::memory_order_release);
    if (!InstallHooks()) {
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

    if (std::strcmp(key, "level_exp_enabled") == 0) {
        if (value->type != DmOptionType::Toggle ||
            (value->bool_value != FALSE && value->bool_value != TRUE)) return FALSE;
        InterlockedExchange(&g_level_multiplier_enabled,
                            value->bool_value != FALSE ? TRUE : FALSE);
        return TRUE;
    }
    if (std::strcmp(key, "item_points_enabled") == 0) {
        if (value->type != DmOptionType::Toggle ||
            (value->bool_value != FALSE && value->bool_value != TRUE)) return FALSE;
        InterlockedExchange(&g_item_point_multiplier_enabled,
                            value->bool_value != FALSE ? TRUE : FALSE);
        return TRUE;
    }

    if (value->type != DmOptionType::SliderFloat || !std::isfinite(value->float_value) ||
        value->float_value < 1.0F || value->float_value > 20.0F) return FALSE;

    const LONG scaled = static_cast<LONG>(std::lround(value->float_value * kMultiplierScale));
    if (std::strcmp(key, "level_exp_multiplier") == 0) {
        InterlockedExchange(&g_level_multiplier_scaled, scaled);
        return TRUE;
    }
    if (std::strcmp(key, "item_point_multiplier") == 0) {
        InterlockedExchange(&g_item_point_multiplier_scaled, scaled);
        return TRUE;
    }
    return FALSE;
}

__declspec(dllexport) void WINAPI Mod_Shutdown() {
    InterlockedExchange(&g_enabled, FALSE);
    RemoveHooks();
    g_exe_base = 0;
    g_log = nullptr;
}

}  // extern "C"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
