#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "../../native/mod_loader/dm_mod_common.h"

namespace {

constexpr std::uintptr_t kApplyRewardsRva = 0x001D77E0;
constexpr std::uintptr_t kAccumulateItemPointsRva = 0x001D7BD0;
constexpr std::uintptr_t kGenerateRarityRva = 0x001D58A0;
constexpr std::uintptr_t kItemWorldRarityReturnRvas[] = {
    0x003C64D3,
    0x003F71CB,
    0x003F75F3,
};
constexpr std::uintptr_t kItemWorldVtableRva = 0x00A251F0;
constexpr std::size_t kLevelProgressOffset = 0x68;
constexpr LONG kMultiplierScale = 1000;
constexpr float kLevelMultiplierMax = 20.0F;
constexpr float kItemPointMultiplierMax = 200.0F;

using ApplyRewardsFn = void (*)(void* item_world, void* result_context);
using AccumulateItemPointsFn = void (*)(void* item_world, std::int64_t base_points);
using GenerateRarityFn = std::int32_t (*)(const std::int32_t* parameters);

void* g_apply_rewards_target = nullptr;
void* g_accumulate_item_points_target = nullptr;
void* g_generate_rarity_target = nullptr;
ApplyRewardsFn g_original_apply_rewards = nullptr;
AccumulateItemPointsFn g_original_accumulate_item_points = nullptr;
GenerateRarityFn g_original_generate_rarity = nullptr;
bool g_hooks_created = false;
volatile LONG g_enabled = FALSE;
volatile LONG g_level_multiplier_enabled = TRUE;
volatile LONG g_item_point_multiplier_enabled = TRUE;
volatile LONG g_rarity_enabled = FALSE;
volatile LONG g_rarity_global = FALSE;
volatile LONG g_level_multiplier_scaled = kMultiplierScale;
volatile LONG g_item_point_multiplier_scaled = kMultiplierScale;
volatile LONG g_minimum_rarity = 50;
volatile LONG g_invalid_object_logged = FALSE;
std::atomic<LONG> g_active_calls{0};
std::atomic<bool> g_shutting_down{false};
SRWLOCK g_reward_lock = SRWLOCK_INIT;
dm::HostLog Log;

void LogInvalidObjectOnce() {
    if (InterlockedCompareExchange(&g_invalid_object_logged, TRUE, FALSE) == FALSE) {
        Log("Recompensa ignorada: objeto CItemWorldData invalido para esta build.");
    }
}

bool IsExpectedItemWorldObject(void* item_world) {
    return dm::HasVtable(reinterpret_cast<std::uintptr_t>(item_world), kItemWorldVtableRva);
}

std::int64_t ScalePositiveValue(std::int64_t value, LONG multiplier) {
    return dm::ScalePositive(value, multiplier, kMultiplierScale,
                             std::numeric_limits<std::int64_t>::max());
}

void HookApplyRewards(void* item_world, void* result_context) {
    dm::CallGuard scope(g_active_calls);
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
        !dm::IsWritableRange(reinterpret_cast<void*>(progress_address), sizeof(std::int32_t))) {
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
    dm::CallGuard scope(g_active_calls);
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

bool IsItemWorldRarityCaller(std::uintptr_t return_address) {
    for (std::uintptr_t rva : kItemWorldRarityReturnRvas) {
        if (return_address == dm::Rva(rva)) return true;
    }
    return false;
}

std::int32_t HookGenerateRarity(const std::int32_t* parameters) {
    dm::CallGuard scope(g_active_calls);
    if (g_original_generate_rarity == nullptr) return -1;

    const auto return_address = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    std::int32_t rarity = g_original_generate_rarity(parameters);
    if (g_shutting_down.load(std::memory_order_acquire) ||
        InterlockedCompareExchange(&g_enabled, FALSE, FALSE) == FALSE ||
        InterlockedCompareExchange(&g_rarity_enabled, FALSE, FALSE) == FALSE ||
        rarity < 0 || rarity > 100) {
        return rarity;
    }
    if (InterlockedCompareExchange(&g_rarity_global, FALSE, FALSE) == FALSE &&
        !IsItemWorldRarityCaller(return_address)) {
        return rarity;
    }

    const LONG minimum = InterlockedCompareExchange(&g_minimum_rarity, 0, 0);
    return rarity < minimum ? minimum : rarity;
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
    static const std::uint8_t expected_rarity_prologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x11,
        0x33, 0xC0, 0x48, 0x8B, 0xD9, 0x85, 0xD2, 0x0F
    };

    g_apply_rewards_target = reinterpret_cast<void*>(dm::Rva(kApplyRewardsRva));
    g_accumulate_item_points_target =
        reinterpret_cast<void*>(dm::Rva(kAccumulateItemPointsRva));
    g_generate_rarity_target = reinterpret_cast<void*>(dm::Rva(kGenerateRarityRva));
    if (!dm::MatchesPrologue(dm::Rva(kApplyRewardsRva), expected_apply_prologue,
                             sizeof(expected_apply_prologue)) ||
        !dm::MatchesPrologue(dm::Rva(kAccumulateItemPointsRva), expected_item_points_prologue,
                             sizeof(expected_item_points_prologue)) ||
        !dm::MatchesPrologue(dm::Rva(kGenerateRarityRva), expected_rarity_prologue,
                             sizeof(expected_rarity_prologue))) {
        g_apply_rewards_target = nullptr;
        g_accumulate_item_points_target = nullptr;
        g_generate_rarity_target = nullptr;
        return false;
    }

    if (!dm::CreateHook(g_apply_rewards_target, reinterpret_cast<void*>(&HookApplyRewards),
                        reinterpret_cast<void**>(&g_original_apply_rewards)) ||
        !dm::CreateHook(g_accumulate_item_points_target,
                        reinterpret_cast<void*>(&HookAccumulateItemPoints),
                        reinterpret_cast<void**>(&g_original_accumulate_item_points)) ||
        !dm::CreateHook(g_generate_rarity_target,
                        reinterpret_cast<void*>(&HookGenerateRarity),
                        reinterpret_cast<void**>(&g_original_generate_rarity))) {
        dm::RemoveHook(g_apply_rewards_target);
        dm::RemoveHook(g_accumulate_item_points_target);
        dm::RemoveHook(g_generate_rarity_target);
        g_original_apply_rewards = nullptr;
        g_original_accumulate_item_points = nullptr;
        g_original_generate_rarity = nullptr;
        g_apply_rewards_target = nullptr;
        g_accumulate_item_points_target = nullptr;
        g_generate_rarity_target = nullptr;
        return false;
    }
    g_hooks_created = true;
    return true;
}

bool SyncHookStates() {
    if (!g_hooks_created) return false;
    const bool mod_enabled = InterlockedCompareExchange(&g_enabled, FALSE, FALSE) != FALSE;
    const struct {
        void* target;
        volatile LONG* option;
    } gates[] = {
        {g_apply_rewards_target, &g_level_multiplier_enabled},
        {g_accumulate_item_points_target, &g_item_point_multiplier_enabled},
        {g_generate_rarity_target, &g_rarity_enabled},
    };

    for (const auto& gate : gates) {
        if (gate.target == nullptr) return false;
        const bool wanted = mod_enabled &&
                            InterlockedCompareExchange(gate.option, FALSE, FALSE) != FALSE;
        if (!dm::QueueHook(gate.target, wanted)) return false;
    }
    return dm::ApplyHooks();
}

void RemoveHooks() {
    if (!g_hooks_created) return;
    g_shutting_down.store(true, std::memory_order_release);
    InterlockedExchange(&g_enabled, FALSE);
    SyncHookStates();
    dm::DrainActiveCalls(g_active_calls);
    dm::RemoveHook(g_apply_rewards_target);
    dm::RemoveHook(g_accumulate_item_points_target);
    dm::RemoveHook(g_generate_rarity_target);
    g_original_apply_rewards = nullptr;
    g_original_accumulate_item_points = nullptr;
    g_original_generate_rarity = nullptr;
    g_apply_rewards_target = nullptr;
    g_accumulate_item_points_target = nullptr;
    g_generate_rarity_target = nullptr;
    g_hooks_created = false;
}

}  // namespace

extern "C" {

__declspec(dllexport) std::uint32_t WINAPI Mod_GetAbiVersion() {
    return DM_MOD_LOADER_ABI_VERSION;
}

__declspec(dllexport) BOOL WINAPI Mod_Initialize(const DmModHostContext* context) {
    if (!dm::AcceptHostContext(context, "item_world", true)) return FALSE;
    Log.Bind(context->loader, "item_world");
    g_shutting_down.store(false, std::memory_order_release);
    if (!InstallHooks()) {
        Log("Falha ao validar ou instalar os hooks do Item World.");
        return FALSE;
    }
    return TRUE;
}

__declspec(dllexport) BOOL WINAPI Mod_Enable() {
    InterlockedExchange(&g_enabled, TRUE);
    if (!SyncHookStates()) {
        InterlockedExchange(&g_enabled, FALSE);
        SyncHookStates();
        Log("Falha ao ativar os hooks do Item World.");
        return FALSE;
    }
    return TRUE;
}

__declspec(dllexport) BOOL WINAPI Mod_Disable() {
    InterlockedExchange(&g_enabled, FALSE);
    SyncHookStates();
    return TRUE;
}

__declspec(dllexport) BOOL WINAPI Mod_SetOption(const char* key, const DmModValue* value) {
    if (key == nullptr || value == nullptr || value->struct_size != sizeof(DmModValue)) return FALSE;

    if (std::strcmp(key, "level_exp_enabled") == 0) {
        if (value->type != DmOptionType::Toggle ||
            (value->bool_value != FALSE && value->bool_value != TRUE)) return FALSE;
        InterlockedExchange(&g_level_multiplier_enabled,
                            value->bool_value != FALSE ? TRUE : FALSE);
        SyncHookStates();
        return TRUE;
    }
    if (std::strcmp(key, "item_points_enabled") == 0) {
        if (value->type != DmOptionType::Toggle ||
            (value->bool_value != FALSE && value->bool_value != TRUE)) return FALSE;
        InterlockedExchange(&g_item_point_multiplier_enabled,
                            value->bool_value != FALSE ? TRUE : FALSE);
        SyncHookStates();
        return TRUE;
    }
    if (std::strcmp(key, "rarity_enabled") == 0) {
        if (value->type != DmOptionType::Toggle ||
            (value->bool_value != FALSE && value->bool_value != TRUE)) return FALSE;
        InterlockedExchange(&g_rarity_enabled,
                            value->bool_value != FALSE ? TRUE : FALSE);
        SyncHookStates();
        return TRUE;
    }
    if (std::strcmp(key, "rarity_global") == 0) {
        if (value->type != DmOptionType::Toggle ||
            (value->bool_value != FALSE && value->bool_value != TRUE)) return FALSE;
        InterlockedExchange(&g_rarity_global,
                            value->bool_value != FALSE ? TRUE : FALSE);
        return TRUE;
    }
    if (std::strcmp(key, "minimum_rarity") == 0) {
        if (value->type != DmOptionType::SliderInt ||
            value->int_value < 0 || value->int_value > 100) return FALSE;
        InterlockedExchange(&g_minimum_rarity, value->int_value);
        return TRUE;
    }

    const bool is_level = std::strcmp(key, "level_exp_multiplier") == 0;
    const bool is_item_points = std::strcmp(key, "item_point_multiplier") == 0;
    if (!is_level && !is_item_points) return FALSE;

    const float maximum = is_item_points ? kItemPointMultiplierMax : kLevelMultiplierMax;
    if (value->type != DmOptionType::SliderFloat || !std::isfinite(value->float_value) ||
        value->float_value < 1.0F || value->float_value > maximum) return FALSE;

    const LONG scaled = static_cast<LONG>(std::lround(value->float_value * kMultiplierScale));
    InterlockedExchange(
        is_level ? &g_level_multiplier_scaled : &g_item_point_multiplier_scaled, scaled);
    return TRUE;
}

__declspec(dllexport) void WINAPI Mod_Shutdown() {
    InterlockedExchange(&g_enabled, FALSE);
    RemoveHooks();
    Log.Reset();
    dm::ReleaseHost();
}

}  // extern "C"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
