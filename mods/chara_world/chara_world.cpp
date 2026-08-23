#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "../../native/mod_menu_overlay/vendor/minhook/include/MinHook.h"
#include "../../native/mod_loader/dm_mod_common.h"

namespace {

constexpr std::uintptr_t kTurnResolveRva = 0x00461CA0;
constexpr std::uintptr_t kInformationSyncRva = 0x00453050;
constexpr std::uintptr_t kParamBonusApplyRva = 0x00458930;
constexpr std::uintptr_t kCharacterWorldInformationVtableRva = 0x00A57610;
constexpr std::uintptr_t kCharacterWorldBonusVtableRva = 0x00A57620;
constexpr std::uintptr_t kEnergyValueVtableRva = 0x00A1A7B0;
constexpr std::size_t kInformationPointerOffset = 0x200;
constexpr std::size_t kParamBonusInformationPointerOffset = 0x218;
constexpr std::size_t kEnergyValueOffset = 0x170;
constexpr std::size_t kCurrentEnergyOffset = 0x178;
constexpr std::size_t kEffectPointerOffset = 0x160;
constexpr std::size_t kBonusPointerOffset = 0x180;
constexpr std::size_t kEffectDescriptorPointerOffset = 0x10;
constexpr std::size_t kEffectDataPointerOffset = 0x20;
constexpr std::size_t kEffectValueOffset = 0x58;
constexpr std::size_t kEffectTypeOffset = 0x44;
constexpr std::uint32_t kParamBonusEffectId = 0x15;
constexpr std::int32_t kMultiplierScale = 1000;

using TurnResolveFn = bool (*)(void* task);
using InformationSyncFn = void (*)(void* information);
using ParamBonusApplyFn = void (*)(void* task);

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_freeze_energy{true};
std::atomic<std::int32_t> g_target_energy{100};
std::atomic<std::int32_t> g_tile_status_multiplier_scaled{kMultiplierScale};
std::atomic<LONG> g_active_calls{0};
std::atomic<bool> g_shutting_down{false};

void* g_turn_hook_target = nullptr;
void* g_information_hook_target = nullptr;
void* g_param_bonus_hook_target = nullptr;
TurnResolveFn g_original_turn_resolve = nullptr;
InformationSyncFn g_original_information_sync = nullptr;
ParamBonusApplyFn g_original_param_bonus_apply = nullptr;
bool g_minhook_initialized = false;
dm::HostLog Log;
SRWLOCK g_param_bonus_lock = SRWLOCK_INIT;

std::int32_t* ResolveCurrentEnergyFromInformation(std::uintptr_t information) {
    if (!dm::IsWritableRange(reinterpret_cast<void*>(information),
                             kCurrentEnergyOffset + sizeof(std::int32_t))) {
        return nullptr;
    }

    if (!dm::HasVtable(information, kCharacterWorldInformationVtableRva) ||
        *reinterpret_cast<std::uintptr_t*>(information + kEnergyValueOffset) !=
            dm::Rva(kEnergyValueVtableRva)) {
        return nullptr;
    }

    return reinterpret_cast<std::int32_t*>(information + kCurrentEnergyOffset);
}

std::int32_t* ResolveCurrentEnergyFromTask(void* task) {
    if (!dm::IsReadableRange(task, kInformationPointerOffset + sizeof(void*))) return nullptr;
    const auto task_address = reinterpret_cast<std::uintptr_t>(task);
    const auto information = *reinterpret_cast<std::uintptr_t*>(
        task_address + kInformationPointerOffset);
    return ResolveCurrentEnergyFromInformation(information);
}

struct ParamBonusDelta {
    std::uintptr_t effect = 0;
    std::int32_t type = 0;
    std::int64_t* value = nullptr;
};

bool ResolveParamBonusDelta(void* task, ParamBonusDelta& output) {
    if (!dm::IsReadableRange(task, kParamBonusInformationPointerOffset + sizeof(void*))) {
        return false;
    }

    const auto task_address = reinterpret_cast<std::uintptr_t>(task);
    const auto information = *reinterpret_cast<std::uintptr_t*>(
        task_address + kParamBonusInformationPointerOffset);
    if (!dm::IsReadableRange(reinterpret_cast<void*>(information),
                             kBonusPointerOffset + sizeof(void*)) ||
        !dm::HasVtable(information, kCharacterWorldInformationVtableRva)) {
        return false;
    }

    const auto bonus = *reinterpret_cast<std::uintptr_t*>(information + kBonusPointerOffset);
    if (!dm::HasVtable(bonus, kCharacterWorldBonusVtableRva)) return false;

    const auto effect = *reinterpret_cast<std::uintptr_t*>(information + kEffectPointerOffset);
    if (!dm::IsWritableRange(reinterpret_cast<void*>(effect),
                             kEffectValueOffset + sizeof(std::int64_t))) {
        return false;
    }

    const auto descriptor = *reinterpret_cast<std::uintptr_t*>(
        effect + kEffectDescriptorPointerOffset);
    if (!dm::IsReadableRange(reinterpret_cast<void*>(descriptor), sizeof(void*))) {
        return false;
    }
    const auto record = *reinterpret_cast<std::uintptr_t*>(descriptor);
    if (!dm::IsReadableRange(reinterpret_cast<void*>(record), 0x8C) ||
        *reinterpret_cast<std::uint32_t*>(record + 0x88) != kParamBonusEffectId) {
        return false;
    }

    const auto effect_data = *reinterpret_cast<std::uintptr_t*>(
        effect + kEffectDataPointerOffset);
    if (!dm::IsReadableRange(reinterpret_cast<void*>(effect_data),
                             kEffectTypeOffset + sizeof(std::int32_t))) {
        return false;
    }
    const std::int32_t type = *reinterpret_cast<std::int32_t*>(
        effect_data + kEffectTypeOffset);
    if (type < 0 || type > 4) return false;

    output.effect = effect;
    output.type = type;
    output.value = reinterpret_cast<std::int64_t*>(effect + kEffectValueOffset);
    return true;
}

std::int64_t ScalePositiveBonus(std::int64_t value, std::int32_t type,
                                std::int32_t multiplier) {
    const std::int64_t maximum = type <= 2
        ? std::numeric_limits<std::int64_t>::max()
        : std::numeric_limits<std::int32_t>::max();
    return dm::ScalePositive(value, multiplier, kMultiplierScale, maximum);
}

void ApplyConfiguredEnergy(std::int32_t* energy) {
    if (!g_enabled.load(std::memory_order_acquire) ||
        !g_freeze_energy.load(std::memory_order_acquire)) {
        return;
    }

    if (energy != nullptr) {
        *energy = g_target_energy.load(std::memory_order_relaxed);
    }
}

bool HookTurnResolve(void* task) {
    dm::CallGuard scope(g_active_calls);
    ApplyConfiguredEnergy(ResolveCurrentEnergyFromTask(task));
    const bool result = g_original_turn_resolve(task);
    if (!g_shutting_down.load(std::memory_order_acquire)) {
        ApplyConfiguredEnergy(ResolveCurrentEnergyFromTask(task));
    }
    return result;
}

void HookInformationSync(void* information) {
    dm::CallGuard scope(g_active_calls);
    ApplyConfiguredEnergy(ResolveCurrentEnergyFromInformation(
        reinterpret_cast<std::uintptr_t>(information)));
    g_original_information_sync(information);
    if (!g_shutting_down.load(std::memory_order_acquire)) {
        ApplyConfiguredEnergy(ResolveCurrentEnergyFromInformation(
            reinterpret_cast<std::uintptr_t>(information)));
    }
}

void HookParamBonusApply(void* task) {
    dm::CallGuard scope(g_active_calls);
    const std::int32_t multiplier =
        g_tile_status_multiplier_scaled.load(std::memory_order_acquire);
    if (!g_enabled.load(std::memory_order_acquire) ||
        g_shutting_down.load(std::memory_order_acquire) ||
        multiplier <= kMultiplierScale) {
        g_original_param_bonus_apply(task);
        return;
    }

    ParamBonusDelta delta;
    if (!ResolveParamBonusDelta(task, delta)) {
        g_original_param_bonus_apply(task);
        return;
    }

    AcquireSRWLockExclusive(&g_param_bonus_lock);
    const std::int64_t original_value = *delta.value;
    *delta.value = ScalePositiveBonus(original_value, delta.type, multiplier);
    g_original_param_bonus_apply(task);

    if (!g_shutting_down.load(std::memory_order_acquire)) {
        ParamBonusDelta current;
        if (ResolveParamBonusDelta(task, current) && current.effect == delta.effect &&
            current.value == delta.value) {
            *current.value = original_value;
        }
    }
    ReleaseSRWLockExclusive(&g_param_bonus_lock);
}

bool InstallHooks() {
    static const std::uint8_t expected_turn_prologue[] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48,
        0x89, 0x70, 0x18, 0x55, 0x57, 0x41, 0x54, 0x41
    };
    static const std::uint8_t expected_information_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83,
        0xEC, 0x50, 0x48, 0x8B, 0x81, 0x90, 0x02, 0x00
    };
    static const std::uint8_t expected_param_bonus_prologue[] = {
        0x40, 0x55, 0x48, 0x83, 0xEC, 0x20, 0x4C, 0x8B,
        0x81, 0x18, 0x02, 0x00, 0x00, 0x48, 0x8B, 0xE9
    };

    g_turn_hook_target = reinterpret_cast<void*>(dm::Rva(kTurnResolveRva));
    g_information_hook_target = reinterpret_cast<void*>(dm::Rva(kInformationSyncRva));
    g_param_bonus_hook_target = reinterpret_cast<void*>(dm::Rva(kParamBonusApplyRva));
    if (!dm::MatchesPrologue(dm::Rva(kTurnResolveRva), expected_turn_prologue,
                             sizeof(expected_turn_prologue)) ||
        !dm::MatchesPrologue(dm::Rva(kInformationSyncRva), expected_information_prologue,
                             sizeof(expected_information_prologue)) ||
        !dm::MatchesPrologue(dm::Rva(kParamBonusApplyRva), expected_param_bonus_prologue,
                             sizeof(expected_param_bonus_prologue))) {
        Log("Build rejeitada: rotinas do Chara World nao correspondem.");
        g_turn_hook_target = nullptr;
        g_information_hook_target = nullptr;
        g_param_bonus_hook_target = nullptr;
        return false;
    }

    if (MH_Initialize() != MH_OK) {
        Log("Falha ao inicializar MinHook.");
        g_turn_hook_target = nullptr;
        g_information_hook_target = nullptr;
        g_param_bonus_hook_target = nullptr;
        return false;
    }
    g_minhook_initialized = true;

    if (MH_CreateHook(g_turn_hook_target, reinterpret_cast<LPVOID>(&HookTurnResolve),
                      reinterpret_cast<LPVOID*>(&g_original_turn_resolve)) != MH_OK ||
        MH_CreateHook(g_information_hook_target,
                      reinterpret_cast<LPVOID>(&HookInformationSync),
                      reinterpret_cast<LPVOID*>(&g_original_information_sync)) != MH_OK ||
        MH_CreateHook(g_param_bonus_hook_target,
                      reinterpret_cast<LPVOID>(&HookParamBonusApply),
                      reinterpret_cast<LPVOID*>(&g_original_param_bonus_apply)) != MH_OK ||
        MH_QueueEnableHook(g_turn_hook_target) != MH_OK ||
        MH_QueueEnableHook(g_information_hook_target) != MH_OK ||
        MH_QueueEnableHook(g_param_bonus_hook_target) != MH_OK ||
        MH_ApplyQueued() != MH_OK) {
        MH_DisableHook(g_turn_hook_target);
        MH_DisableHook(g_information_hook_target);
        MH_DisableHook(g_param_bonus_hook_target);
        MH_RemoveHook(g_turn_hook_target);
        MH_RemoveHook(g_information_hook_target);
        MH_RemoveHook(g_param_bonus_hook_target);
        MH_Uninitialize();
        g_minhook_initialized = false;
        g_turn_hook_target = nullptr;
        g_information_hook_target = nullptr;
        g_param_bonus_hook_target = nullptr;
        g_original_turn_resolve = nullptr;
        g_original_information_sync = nullptr;
        g_original_param_bonus_apply = nullptr;
        Log("Falha ao instalar os hooks do Chara World.");
        return false;
    }

    return true;
}

void RemoveHook() {
    g_enabled.store(false, std::memory_order_release);
    g_shutting_down.store(true, std::memory_order_release);

    if (g_minhook_initialized) {
        if (g_turn_hook_target != nullptr) MH_DisableHook(g_turn_hook_target);
        if (g_information_hook_target != nullptr) MH_DisableHook(g_information_hook_target);
        if (g_param_bonus_hook_target != nullptr) MH_DisableHook(g_param_bonus_hook_target);
        while (g_active_calls.load(std::memory_order_acquire) != 0) {
            SwitchToThread();
        }
        if (g_turn_hook_target != nullptr) MH_RemoveHook(g_turn_hook_target);
        if (g_information_hook_target != nullptr) MH_RemoveHook(g_information_hook_target);
        if (g_param_bonus_hook_target != nullptr) MH_RemoveHook(g_param_bonus_hook_target);
    }
    if (g_minhook_initialized) MH_Uninitialize();

    g_minhook_initialized = false;
    g_turn_hook_target = nullptr;
    g_information_hook_target = nullptr;
    g_param_bonus_hook_target = nullptr;
    g_original_turn_resolve = nullptr;
    g_original_information_sync = nullptr;
    g_original_param_bonus_apply = nullptr;
}

}  // namespace

extern "C" __declspec(dllexport) std::uint32_t WINAPI Mod_GetAbiVersion() {
    return DM_MOD_LOADER_ABI_VERSION;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Initialize(const DmModHostContext* context) {
    if (!dm::AcceptHostContext(context, true)) return FALSE;
    Log.Bind(context->loader, "chara_world");
    g_shutting_down.store(false, std::memory_order_release);
    return InstallHooks() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Enable() {
    if (!g_minhook_initialized || g_turn_hook_target == nullptr ||
        g_information_hook_target == nullptr || g_param_bonus_hook_target == nullptr) {
        return FALSE;
    }
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

    if (std::strcmp(key, "tile_status_multiplier") == 0) {
        if (value->type != DmOptionType::SliderFloat ||
            !std::isfinite(value->float_value) || value->float_value < 1.0F ||
            value->float_value > 20.0F) {
            return FALSE;
        }
        g_tile_status_multiplier_scaled.store(
            static_cast<std::int32_t>(
                std::lround(value->float_value * kMultiplierScale)),
            std::memory_order_release);
        return TRUE;
    }

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
