#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "../../native/mod_loader/dm_mod_common.h"

namespace {

constexpr std::uintptr_t kWaitEnterRva = 0x0017A5E0;
constexpr std::uintptr_t kAttackWaitEnterRva = 0x0017BF60;
constexpr std::uintptr_t kSearchingEnterRva = 0x00183FF0;
constexpr std::uintptr_t kMovementSetupRva = 0x00175E60;
constexpr std::uintptr_t kAttackRushSetupRva = 0x00180E20;
constexpr std::uintptr_t kSearchingSetupRva = 0x00183C50;
constexpr std::uintptr_t kTacticalStateUpdateRva = 0x0018AF90;
constexpr std::uintptr_t kTacticsManagementUpdateRva = 0x00194AD0;
constexpr std::uintptr_t kControllerFactoryRva = 0x00192950;
constexpr std::uintptr_t kExploreUnitDeletingDestructorRva = 0x00163D60;

constexpr std::uintptr_t kWaitVtableRva = 0x00A1C7C8;
constexpr std::uintptr_t kAttackWaitVtableRva = 0x00A1C540;
constexpr std::uintptr_t kSearchingVtableRva = 0x00A1C188;
constexpr std::uintptr_t kAttackRushVtableRva = 0x00A1C300;
constexpr std::uintptr_t kTacticalStateVtableRva = 0x00A1C030;
constexpr std::uintptr_t kTacticsManagementVtableRva = 0x00A1BEE0;
constexpr std::uintptr_t kTacticsVtableRva = 0x00A1BEF0;
constexpr std::uintptr_t kControllerVtableRva = 0x00A1BF00;
constexpr std::uintptr_t kEnemyStatusVtableRva = 0x00A1BF10;
constexpr std::uintptr_t kExploreUnitVtableRva = 0x00A1D088;

constexpr std::uintptr_t kRebuildControllerReturnRvaA = 0x0013A077;
constexpr std::uintptr_t kRebuildControllerReturnRvaB = 0x0013A1A1;
constexpr std::uintptr_t kCompanionPlayerControllerReturnRva = 0x003CA9BA;
constexpr std::uintptr_t kCompanionKidsControllerReturnRva = 0x003CD491;
constexpr std::uintptr_t kEnemyKidsControllerReturnRva = 0x003CF0AF;
constexpr std::uintptr_t kEnemyControllerReturnRva = 0x003D2C50;
constexpr std::uintptr_t kEnemyManagementUpdateReturnRva = 0x0041D2DD;
constexpr std::uintptr_t kCompanionManagementUpdateReturnRva = 0x0041D2F3;

constexpr LONG kPercentScale = 100;
constexpr LONG kDefaultEnemyAttackIntervalPercent = 40;
constexpr LONG kDefaultEnemyPauseDurationPercent = 30;
constexpr LONG kDefaultEnemySearchIntervalPercent = 45;
constexpr LONG kDefaultEnemyMovementSpeedPercent = 160;
constexpr LONG kDefaultEnemySearchRangePercent = 175;
constexpr LONG kDefaultEnemyRedecisionFrequencyPercent = 250;
constexpr LONG kDefaultCompanionAttackIntervalPercent = 30;
constexpr LONG kDefaultCompanionPauseDurationPercent = 20;
constexpr LONG kDefaultCompanionSearchIntervalPercent = 30;
constexpr LONG kDefaultCompanionMovementSpeedPercent = 180;
constexpr LONG kDefaultCompanionSearchRangePercent = 200;
constexpr LONG kDefaultCompanionRedecisionFrequencyPercent = 300;
constexpr std::uint64_t kMaximumDuration = 3600000;
constexpr std::uint64_t kMaximumRelotteryDuration = 0x0FFFFFFFFFFFFFFFULL;
constexpr float kMaximumScalar = 1000000.0F;
constexpr std::size_t kMaximumSnapshots = 512;
constexpr std::size_t kMaximumUnitIdentities = 1024;
constexpr std::size_t kMaximumFields = 4;
constexpr std::size_t kMaximumRelotteryNodesPerState = 256;

constexpr std::array<std::size_t, 4> kWaitDurationOffsets = {
    0x40, 0x48, 0x70, 0x78,
};
constexpr std::array<std::size_t, 2> kAttackWaitDurationOffsets = {
    0x1A8, 0x1B0,
};
constexpr std::array<std::size_t, 2> kSearchingDurationOffsets = {
    0x70, 0x78,
};
constexpr std::array<std::size_t, 3> kMovementScalarOffsets = {
    0x90, 0x98, 0x9C,
};
constexpr std::array<std::size_t, 1> kRushScalarOffsets = {0x1F0};
constexpr std::array<std::size_t, 1> kSearchRangeScalarOffsets = {0x1B4};
constexpr std::array<std::uintptr_t, 12> kExecutionStateVtableRvas = {
    0x00A1C9F0, 0x00A1C938, 0x00A1C880, 0x00A1C7C8,
    0x00A1C710, 0x00A1C540, 0x00A1C480, 0x00A1C3C0,
    0x00A1C300, 0x00A1C240, 0x00A1C188, 0x00A1C0C0,
};

using StateEnterFn = void (*)(void* state);
using StateSetupFn = void (*)(void* state);
using TacticalStateUpdateFn = void (*)(void* state, const void* update_info);
using TacticsManagementUpdateFn = void (*)(void* management,
                                            const void* update_info);
using ControllerFactoryFn = void* (*)(void* owner, void* output,
                                      void* tactics_data, void* unit);
using ExploreUnitDeletingDestructorFn = void* (*)(void* unit,
                                                  unsigned int flags);

enum class UnitSide : std::uint8_t {
    Enemy,
    Companion,
};

enum class FactoryOrigin : std::uint8_t {
    Invalid,
    ExistingUnit,
    Enemy,
    Companion,
};

enum class StateKind : std::uint8_t {
    Wait,
    AttackWait,
    Searching,
};

enum class ScalarKind : std::uint8_t {
    Movement,
    Rush,
    SearchRange,
};

struct StateSnapshot {
    std::uintptr_t state = 0;
    StateKind kind = StateKind::Wait;
    UnitSide side = UnitSide::Enemy;
    std::size_t field_count = 0;
    std::array<std::uint64_t, kMaximumFields> original{};
    std::array<std::uint64_t, kMaximumFields> applied{};
    bool valid = false;
};

struct ScalarSnapshot {
    std::uintptr_t state = 0;
    std::uintptr_t vtable = 0;
    ScalarKind kind = ScalarKind::Movement;
    UnitSide side = UnitSide::Enemy;
    std::size_t field_count = 0;
    std::array<std::uint32_t, 3> original{};
    std::array<std::uint32_t, 3> applied{};
    bool valid = false;
};

struct RelotterySnapshot {
    std::uintptr_t state = 0;
    std::uintptr_t node = 0;
    UnitSide side = UnitSide::Enemy;
    std::uint64_t original = 0;
    std::uint64_t applied = 0;
    bool valid = false;
};

struct UnitIdentity {
    std::uintptr_t unit = 0;
    std::uintptr_t controller = 0;
    std::uintptr_t status = 0;
    UnitSide side = UnitSide::Enemy;
    bool valid = false;
};

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_shutting_down{false};
std::atomic<bool> g_state_hooks_active{false};
std::atomic<bool> g_identity_hooks_active{false};
std::atomic<bool> g_runtime_faulted{false};
std::atomic<bool> g_identity_faulted{false};
std::atomic<LONG> g_state_active_calls{0};
std::atomic<LONG> g_identity_active_calls{0};
std::atomic<bool> g_enemy_ai_enabled{true};
std::atomic<bool> g_companion_ai_enabled{true};
std::atomic<bool> g_enemy_profile_enabled{true};
std::atomic<bool> g_companion_profile_enabled{true};
std::atomic<std::uintptr_t> g_enemy_tactics_management{0};
std::atomic<std::uintptr_t> g_companion_tactics_management{0};
std::atomic<LONG> g_enemy_attack_interval_percent{
    kDefaultEnemyAttackIntervalPercent};
std::atomic<LONG> g_enemy_pause_duration_percent{
    kDefaultEnemyPauseDurationPercent};
std::atomic<LONG> g_enemy_search_interval_percent{
    kDefaultEnemySearchIntervalPercent};
std::atomic<LONG> g_enemy_movement_speed_percent{
    kDefaultEnemyMovementSpeedPercent};
std::atomic<LONG> g_enemy_search_range_percent{
    kDefaultEnemySearchRangePercent};
std::atomic<LONG> g_enemy_redecision_frequency_percent{
    kDefaultEnemyRedecisionFrequencyPercent};
std::atomic<LONG> g_companion_attack_interval_percent{
    kDefaultCompanionAttackIntervalPercent};
std::atomic<LONG> g_companion_pause_duration_percent{
    kDefaultCompanionPauseDurationPercent};
std::atomic<LONG> g_companion_search_interval_percent{
    kDefaultCompanionSearchIntervalPercent};
std::atomic<LONG> g_companion_movement_speed_percent{
    kDefaultCompanionMovementSpeedPercent};
std::atomic<LONG> g_companion_search_range_percent{
    kDefaultCompanionSearchRangePercent};
std::atomic<LONG> g_companion_redecision_frequency_percent{
    kDefaultCompanionRedecisionFrequencyPercent};
std::atomic<std::uint64_t> g_enemy_attack_wait_entries{0};
std::atomic<std::uint64_t> g_enemy_tactical_wait_entries{0};
std::atomic<std::uint64_t> g_enemy_search_entries{0};
std::atomic<std::uint64_t> g_companion_attack_wait_entries{0};
std::atomic<std::uint64_t> g_companion_tactical_wait_entries{0};
std::atomic<std::uint64_t> g_companion_search_entries{0};
std::atomic<bool> g_enemy_attack_sample_logged{false};
std::atomic<bool> g_enemy_wait_sample_logged{false};
std::atomic<bool> g_enemy_search_sample_logged{false};
std::atomic<bool> g_companion_attack_sample_logged{false};
std::atomic<bool> g_companion_wait_sample_logged{false};
std::atomic<bool> g_companion_search_sample_logged{false};
std::atomic<bool> g_snapshot_capacity_logged{false};
std::atomic<bool> g_invalid_state_logged{false};
std::atomic<bool> g_enemy_movement_sample_logged{false};
std::atomic<bool> g_companion_movement_sample_logged{false};
std::atomic<bool> g_enemy_search_range_sample_logged{false};
std::atomic<bool> g_companion_search_range_sample_logged{false};
std::atomic<bool> g_enemy_redecision_sample_logged{false};
std::atomic<bool> g_companion_redecision_sample_logged{false};
std::atomic<bool> g_enemy_ai_block_logged{false};
std::atomic<bool> g_companion_ai_block_logged{false};

void* g_wait_target = nullptr;
void* g_attack_wait_target = nullptr;
void* g_searching_target = nullptr;
void* g_movement_setup_target = nullptr;
void* g_attack_rush_setup_target = nullptr;
void* g_searching_setup_target = nullptr;
void* g_tactical_state_update_target = nullptr;
void* g_tactics_management_update_target = nullptr;
void* g_controller_factory_target = nullptr;
void* g_explore_unit_destructor_target = nullptr;
StateEnterFn g_original_wait_enter = nullptr;
StateEnterFn g_original_attack_wait_enter = nullptr;
StateEnterFn g_original_searching_enter = nullptr;
StateSetupFn g_original_movement_setup = nullptr;
StateSetupFn g_original_attack_rush_setup = nullptr;
StateSetupFn g_original_searching_setup = nullptr;
TacticalStateUpdateFn g_original_tactical_state_update = nullptr;
TacticsManagementUpdateFn g_original_tactics_management_update = nullptr;
ControllerFactoryFn g_original_controller_factory = nullptr;
ExploreUnitDeletingDestructorFn g_original_explore_unit_destructor = nullptr;
bool g_hooks_created = false;
SRWLOCK g_state_lock = SRWLOCK_INIT;
SRWLOCK g_identity_lock = SRWLOCK_INIT;
std::array<StateSnapshot, kMaximumSnapshots> g_snapshots{};
std::array<ScalarSnapshot, kMaximumSnapshots> g_scalar_snapshots{};
std::array<RelotterySnapshot, kMaximumSnapshots> g_relottery_snapshots{};
std::array<UnitIdentity, kMaximumUnitIdentities> g_unit_identities{};
dm::HostLog Log;

std::uintptr_t ExpectedVtable(StateKind kind) {
    switch (kind) {
        case StateKind::Wait: return dm::Rva(kWaitVtableRva);
        case StateKind::AttackWait: return dm::Rva(kAttackWaitVtableRva);
        case StateKind::Searching: return dm::Rva(kSearchingVtableRva);
    }
    return 0;
}

bool IsProfileEnabled(UnitSide side) {
    return side == UnitSide::Companion
               ? g_companion_profile_enabled.load(std::memory_order_acquire)
               : g_enemy_profile_enabled.load(std::memory_order_acquire);
}

bool IsAiEnabled(UnitSide side) {
    return side == UnitSide::Companion
               ? g_companion_ai_enabled.load(std::memory_order_acquire)
               : g_enemy_ai_enabled.load(std::memory_order_acquire);
}

LONG CurrentDurationPercent(StateKind kind, UnitSide side) {
    if (side == UnitSide::Companion) {
        switch (kind) {
            case StateKind::Wait:
                return g_companion_pause_duration_percent.load(
                    std::memory_order_acquire);
            case StateKind::AttackWait:
                return g_companion_attack_interval_percent.load(
                    std::memory_order_acquire);
            case StateKind::Searching:
                return g_companion_search_interval_percent.load(
                    std::memory_order_acquire);
        }
    } else {
        switch (kind) {
            case StateKind::Wait:
                return g_enemy_pause_duration_percent.load(
                    std::memory_order_acquire);
            case StateKind::AttackWait:
                return g_enemy_attack_interval_percent.load(
                    std::memory_order_acquire);
            case StateKind::Searching:
                return g_enemy_search_interval_percent.load(
                    std::memory_order_acquire);
        }
    }
    return kPercentScale;
}

LONG CurrentScalarPercent(ScalarKind kind, UnitSide side) {
    if (side == UnitSide::Companion) {
        return kind == ScalarKind::SearchRange
                   ? g_companion_search_range_percent.load(
                         std::memory_order_acquire)
                   : g_companion_movement_speed_percent.load(
                         std::memory_order_acquire);
    }
    return kind == ScalarKind::SearchRange
               ? g_enemy_search_range_percent.load(std::memory_order_acquire)
               : g_enemy_movement_speed_percent.load(std::memory_order_acquire);
}

LONG CurrentRedecisionFrequency(UnitSide side) {
    return side == UnitSide::Companion
               ? g_companion_redecision_frequency_percent.load(
                     std::memory_order_acquire)
               : g_enemy_redecision_frequency_percent.load(
                     std::memory_order_acquire);
}

const std::size_t* DurationOffsets(StateKind kind, std::size_t& count) {
    switch (kind) {
        case StateKind::Wait:
            count = kWaitDurationOffsets.size();
            return kWaitDurationOffsets.data();
        case StateKind::AttackWait:
            count = kAttackWaitDurationOffsets.size();
            return kAttackWaitDurationOffsets.data();
        case StateKind::Searching:
            count = kSearchingDurationOffsets.size();
            return kSearchingDurationOffsets.data();
    }
    count = 0;
    return nullptr;
}

std::uint64_t ScaleDuration(std::uint64_t value, LONG percent) {
    if (value == 0 || percent == kPercentScale) return value;
    const std::uint64_t whole =
        (value / kPercentScale) * static_cast<std::uint64_t>(percent);
    const std::uint64_t remainder =
        ((value % kPercentScale) * static_cast<std::uint64_t>(percent) +
         kPercentScale - 1) /
        kPercentScale;
    if (whole > kMaximumDuration || remainder > kMaximumDuration - whole) {
        return kMaximumDuration;
    }
    const std::uint64_t scaled = whole + remainder;
    return scaled == 0 ? 1 : scaled;
}

std::uint64_t ScaleDurationByFrequency(std::uint64_t value, LONG frequency) {
    if (value == 0 || frequency == kPercentScale) return value;
    const std::uint64_t whole =
        (value / static_cast<std::uint64_t>(frequency)) * kPercentScale;
    const std::uint64_t remainder =
        ((value % static_cast<std::uint64_t>(frequency)) * kPercentScale +
         static_cast<std::uint64_t>(frequency) - 1) /
        static_cast<std::uint64_t>(frequency);
    if (whole > kMaximumRelotteryDuration ||
        remainder > kMaximumRelotteryDuration - whole) {
        return kMaximumRelotteryDuration;
    }
    const std::uint64_t scaled = whole + remainder;
    return scaled == 0 ? 1 : scaled;
}

bool ReadDurations(std::uintptr_t state, StateKind kind,
                   std::array<std::uint64_t, kMaximumFields>& values,
                   std::size_t& count) {
    const std::size_t* offsets = DurationOffsets(kind, count);
    if (state == 0 || offsets == nullptr || count == 0 ||
        !dm::IsWritableRange(reinterpret_cast<void*>(state),
                             offsets[count - 1] + sizeof(std::uint64_t)) ||
        *reinterpret_cast<const std::uintptr_t*>(state) != ExpectedVtable(kind)) {
        return false;
    }
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint64_t value = *reinterpret_cast<const std::uint64_t*>(
            state + offsets[index]);
        if (value > kMaximumDuration) return false;
        values[index] = value;
    }
    return true;
}

void WriteDurations(std::uintptr_t state, StateKind kind,
                    const std::array<std::uint64_t, kMaximumFields>& values,
                    std::size_t count) {
    std::size_t expected_count = 0;
    const std::size_t* offsets = DurationOffsets(kind, expected_count);
    if (offsets == nullptr || count != expected_count) return;
    for (std::size_t index = 0; index < count; ++index) {
        *reinterpret_cast<std::uint64_t*>(state + offsets[index]) = values[index];
    }
}

bool IsKnownExecutionStateVtable(std::uintptr_t vtable) {
    for (const std::uintptr_t rva : kExecutionStateVtableRvas) {
        if (vtable == dm::Rva(rva)) return true;
    }
    return false;
}

const std::size_t* ScalarOffsets(ScalarKind kind, std::size_t& count) {
    switch (kind) {
        case ScalarKind::Movement:
            count = kMovementScalarOffsets.size();
            return kMovementScalarOffsets.data();
        case ScalarKind::Rush:
            count = kRushScalarOffsets.size();
            return kRushScalarOffsets.data();
        case ScalarKind::SearchRange:
            count = kSearchRangeScalarOffsets.size();
            return kSearchRangeScalarOffsets.data();
    }
    count = 0;
    return nullptr;
}

bool ReadScalars(std::uintptr_t state, ScalarKind kind,
                 std::array<std::uint32_t, 3>& values,
                 std::size_t& count, std::uintptr_t& vtable) {
    const std::size_t* offsets = ScalarOffsets(kind, count);
    if (state == 0 || offsets == nullptr || count == 0 ||
        !dm::IsWritableRange(reinterpret_cast<void*>(state),
                             offsets[count - 1] + sizeof(std::uint32_t))) {
        return false;
    }
    vtable = *reinterpret_cast<const std::uintptr_t*>(state);
    if (!IsKnownExecutionStateVtable(vtable) ||
        (kind == ScalarKind::Rush &&
         vtable != dm::Rva(kAttackRushVtableRva)) ||
        (kind == ScalarKind::SearchRange &&
         vtable != dm::Rva(kSearchingVtableRva))) {
        return false;
    }
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint32_t raw = *reinterpret_cast<const std::uint32_t*>(
            state + offsets[index]);
        float scalar = 0.0F;
        std::memcpy(&scalar, &raw, sizeof(scalar));
        if (!std::isfinite(scalar) || scalar < 0.0F ||
            scalar > kMaximumScalar) {
            return false;
        }
        values[index] = raw;
    }
    return true;
}

void WriteScalars(std::uintptr_t state, ScalarKind kind,
                  const std::array<std::uint32_t, 3>& values,
                  std::size_t count) {
    std::size_t expected_count = 0;
    const std::size_t* offsets = ScalarOffsets(kind, expected_count);
    if (offsets == nullptr || count != expected_count) return;
    for (std::size_t index = 0; index < count; ++index) {
        *reinterpret_cast<std::uint32_t*>(state + offsets[index]) = values[index];
    }
}

bool ScaleScalar(std::uint32_t raw, ScalarKind, LONG percent,
                 std::uint32_t& scaled_raw) {
    float original = 0.0F;
    std::memcpy(&original, &raw, sizeof(original));
    const float scaled = original * static_cast<float>(percent) /
                         static_cast<float>(kPercentScale);
    if (!std::isfinite(scaled) || scaled < 0.0F || scaled > kMaximumScalar) {
        return false;
    }
    std::memcpy(&scaled_raw, &scaled, sizeof(scaled_raw));
    return true;
}

bool IsRelotteryNodeInState(std::uintptr_t state, std::uintptr_t wanted_node) {
    if (!dm::HasVtable(state, kTacticalStateVtableRva) ||
        !dm::IsReadableRange(reinterpret_cast<const void*>(state), 0xB8)) {
        return false;
    }
    const std::uintptr_t sentinel =
        *reinterpret_cast<const std::uintptr_t*>(state + 0xB0);
    if (!dm::IsReadableRange(reinterpret_cast<const void*>(sentinel), 0x10)) {
        return false;
    }
    std::uintptr_t node =
        *reinterpret_cast<const std::uintptr_t*>(sentinel);
    for (std::size_t index = 0; index < kMaximumRelotteryNodesPerState;
         ++index) {
        if (node == sentinel) return false;
        if (!dm::IsReadableRange(reinterpret_cast<const void*>(node), 0x48)) {
            return false;
        }
        if (node == wanted_node) return true;
        node = *reinterpret_cast<const std::uintptr_t*>(node);
    }
    return false;
}

bool ReadRelotteryDuration(std::uintptr_t state, std::uintptr_t node,
                           std::uint64_t& value) {
    if (!IsRelotteryNodeInState(state, node) ||
        !dm::IsWritableRange(reinterpret_cast<void*>(node), 0x38)) {
        return false;
    }
    const std::uint32_t mode =
        *reinterpret_cast<const std::uint32_t*>(node + 0x14);
    if ((mode != 1 && mode != 3) ||
        *reinterpret_cast<const std::uint8_t*>(node + 0x44) == 0) {
        return false;
    }
    value = *reinterpret_cast<const std::uint64_t*>(node + 0x30);
    return value > 0 && value <= kMaximumRelotteryDuration;
}

void WriteRelotteryDuration(std::uintptr_t node, std::uint64_t value) {
    *reinterpret_cast<std::uint64_t*>(node + 0x30) = value;
}

void RestoreSnapshotsLocked() {
    for (StateSnapshot& snapshot : g_snapshots) {
        if (!snapshot.valid) continue;
        std::array<std::uint64_t, kMaximumFields> current{};
        std::size_t count = 0;
        if (ReadDurations(snapshot.state, snapshot.kind, current, count) &&
            count == snapshot.field_count) {
            WriteDurations(snapshot.state, snapshot.kind, snapshot.original,
                           snapshot.field_count);
        }
        snapshot = {};
    }
    for (ScalarSnapshot& snapshot : g_scalar_snapshots) {
        if (!snapshot.valid) continue;
        std::array<std::uint32_t, 3> current{};
        std::size_t count = 0;
        std::uintptr_t vtable = 0;
        if (ReadScalars(snapshot.state, snapshot.kind, current, count, vtable) &&
            count == snapshot.field_count && vtable == snapshot.vtable) {
            WriteScalars(snapshot.state, snapshot.kind, snapshot.original,
                         snapshot.field_count);
        }
        snapshot = {};
    }
    for (RelotterySnapshot& snapshot : g_relottery_snapshots) {
        if (!snapshot.valid) continue;
        std::uint64_t current = 0;
        if (ReadRelotteryDuration(snapshot.state, snapshot.node, current)) {
            WriteRelotteryDuration(snapshot.node, snapshot.original);
        }
        snapshot = {};
    }
}

void RestoreAndClearSnapshots() {
    AcquireSRWLockExclusive(&g_state_lock);
    RestoreSnapshotsLocked();
    ReleaseSRWLockExclusive(&g_state_lock);
}

void FailClosed(const char* message) {
    const bool first_failure =
        !g_runtime_faulted.exchange(true, std::memory_order_acq_rel);
    g_enabled.store(false, std::memory_order_release);
    if (!first_failure) return;
    RestoreAndClearSnapshots();
    Log(message);
}

const char* SideName(UnitSide side) {
    return side == UnitSide::Companion ? "parceiro" : "inimigo";
}

FactoryOrigin IdentifyFactoryOrigin(void* return_address) {
    const auto address = reinterpret_cast<std::uintptr_t>(return_address);
    if (address == dm::Rva(kCompanionPlayerControllerReturnRva) ||
        address == dm::Rva(kCompanionKidsControllerReturnRva)) {
        return FactoryOrigin::Companion;
    }
    if (address == dm::Rva(kEnemyKidsControllerReturnRva) ||
        address == dm::Rva(kEnemyControllerReturnRva)) {
        return FactoryOrigin::Enemy;
    }
    if (address == dm::Rva(kRebuildControllerReturnRvaA) ||
        address == dm::Rva(kRebuildControllerReturnRvaB)) {
        return FactoryOrigin::ExistingUnit;
    }
    return FactoryOrigin::Invalid;
}

bool IdentifyManagementSide(void* return_address, UnitSide& side) {
    const auto address = reinterpret_cast<std::uintptr_t>(return_address);
    if (address == dm::Rva(kEnemyManagementUpdateReturnRva)) {
        side = UnitSide::Enemy;
        return true;
    }
    if (address == dm::Rva(kCompanionManagementUpdateReturnRva)) {
        side = UnitSide::Companion;
        return true;
    }
    return false;
}

bool RegisterTacticsManagement(std::uintptr_t management, UnitSide side) {
    if (!dm::HasVtable(management, kTacticsManagementVtableRva)) return false;
    std::atomic<std::uintptr_t>& destination =
        side == UnitSide::Companion ? g_companion_tactics_management
                                    : g_enemy_tactics_management;
    const std::uintptr_t previous =
        destination.exchange(management, std::memory_order_acq_rel);
    if (previous != management) {
        char message[160] = {};
        std::snprintf(message, sizeof(message),
                      "Gerenciador tatico de %s registrado.", SideName(side));
        Log(message);
    }
    return true;
}

UnitIdentity* FindUnitIdentityLocked(std::uintptr_t unit) {
    for (UnitIdentity& identity : g_unit_identities) {
        if (identity.valid && identity.unit == unit) return &identity;
    }
    return nullptr;
}

bool RegisterUnitIdentity(std::uintptr_t unit, UnitSide side) {
    if (!dm::HasVtable(unit, kExploreUnitVtableRva)) return false;
    AcquireSRWLockExclusive(&g_identity_lock);
    UnitIdentity* slot = FindUnitIdentityLocked(unit);
    if (slot == nullptr) {
        for (UnitIdentity& identity : g_unit_identities) {
            if (!identity.valid) {
                slot = &identity;
                break;
            }
        }
    }
    if (slot != nullptr) {
        slot->unit = unit;
        slot->side = side;
        slot->valid = true;
    }
    ReleaseSRWLockExclusive(&g_identity_lock);
    return slot != nullptr;
}

bool IsRegisteredUnit(std::uintptr_t unit) {
    AcquireSRWLockShared(&g_identity_lock);
    const bool registered = FindUnitIdentityLocked(unit) != nullptr;
    ReleaseSRWLockShared(&g_identity_lock);
    return registered;
}

bool AttachControllerIdentity(std::uintptr_t unit, std::uintptr_t output) {
    if (!dm::IsReadableRange(reinterpret_cast<const void*>(output),
                             sizeof(std::uintptr_t))) {
        return false;
    }
    const std::uintptr_t controller =
        *reinterpret_cast<const std::uintptr_t*>(output);
    if (!dm::HasVtable(controller, kControllerVtableRva) ||
        !dm::IsReadableRange(reinterpret_cast<const void*>(controller), 0x30) ||
        *reinterpret_cast<const std::uintptr_t*>(controller + 0x28) != unit) {
        return false;
    }
    const std::uintptr_t status =
        *reinterpret_cast<const std::uintptr_t*>(controller + 0x18);
    if (!dm::HasVtable(status, kEnemyStatusVtableRva)) return false;

    AcquireSRWLockExclusive(&g_identity_lock);
    UnitIdentity* identity = FindUnitIdentityLocked(unit);
    if (identity != nullptr) {
        identity->controller = controller;
        identity->status = status;
    }
    ReleaseSRWLockExclusive(&g_identity_lock);
    return identity != nullptr;
}

void RemoveUnitIdentity(std::uintptr_t unit) {
    AcquireSRWLockExclusive(&g_identity_lock);
    UnitIdentity* identity = FindUnitIdentityLocked(unit);
    if (identity != nullptr) *identity = {};
    ReleaseSRWLockExclusive(&g_identity_lock);
}

void ClearUnitIdentities() {
    AcquireSRWLockExclusive(&g_identity_lock);
    for (UnitIdentity& identity : g_unit_identities) identity = {};
    ReleaseSRWLockExclusive(&g_identity_lock);
    g_enemy_tactics_management.store(0, std::memory_order_release);
    g_companion_tactics_management.store(0, std::memory_order_release);
}

void IdentityFailClosed(const char* message) {
    g_identity_faulted.store(true, std::memory_order_release);
    FailClosed(message);
}

bool ResolveStateSide(std::uintptr_t state, UnitSide& side) {
    if (!dm::IsReadableRange(reinterpret_cast<const void*>(state), 0x30)) {
        return false;
    }
    const std::uintptr_t controller =
        *reinterpret_cast<const std::uintptr_t*>(state + 0x28);
    if (!dm::HasVtable(controller, kControllerVtableRva) ||
        !dm::IsReadableRange(reinterpret_cast<const void*>(controller), 0x30)) {
        return false;
    }
    const std::uintptr_t unit =
        *reinterpret_cast<const std::uintptr_t*>(controller + 0x28);
    if (!dm::HasVtable(unit, kExploreUnitVtableRva)) return false;

    AcquireSRWLockShared(&g_identity_lock);
    UnitIdentity* identity = FindUnitIdentityLocked(unit);
    if (identity != nullptr) side = identity->side;
    ReleaseSRWLockShared(&g_identity_lock);
    return identity != nullptr;
}

bool ResolveTacticalStateSide(std::uintptr_t state, UnitSide& side) {
    if (!dm::HasVtable(state, kTacticalStateVtableRva) ||
        !dm::IsReadableRange(reinterpret_cast<const void*>(state), 0x30)) {
        return false;
    }
    const std::uintptr_t tactics =
        *reinterpret_cast<const std::uintptr_t*>(state + 0x28);
    if (!dm::HasVtable(tactics, kTacticsVtableRva) ||
        !dm::IsReadableRange(reinterpret_cast<const void*>(tactics), 0xB8)) {
        return false;
    }
    const std::uintptr_t management =
        *reinterpret_cast<const std::uintptr_t*>(tactics + 0xB0);
    if (!dm::HasVtable(management, kTacticsManagementVtableRva)) return false;

    if (management ==
        g_enemy_tactics_management.load(std::memory_order_acquire)) {
        side = UnitSide::Enemy;
        return true;
    }
    if (management ==
        g_companion_tactics_management.load(std::memory_order_acquire)) {
        side = UnitSide::Companion;
        return true;
    }
    return false;
}

StateSnapshot* FindSnapshotSlot(std::uintptr_t state, StateKind kind,
                                UnitSide side) {
    StateSnapshot* reusable = nullptr;
    for (StateSnapshot& snapshot : g_snapshots) {
        if (snapshot.valid && snapshot.state == state && snapshot.kind == kind &&
            snapshot.side == side) {
            return &snapshot;
        }
        if (!snapshot.valid && reusable == nullptr) reusable = &snapshot;
        if (snapshot.valid && reusable == nullptr &&
            (!dm::IsReadableRange(reinterpret_cast<void*>(snapshot.state),
                                  sizeof(std::uintptr_t)) ||
             *reinterpret_cast<const std::uintptr_t*>(snapshot.state) !=
                 ExpectedVtable(snapshot.kind))) {
            snapshot = {};
            reusable = &snapshot;
        }
    }
    return reusable;
}

ScalarSnapshot* FindScalarSnapshotSlot(std::uintptr_t state, ScalarKind kind,
                                       UnitSide side) {
    ScalarSnapshot* reusable = nullptr;
    for (ScalarSnapshot& snapshot : g_scalar_snapshots) {
        if (snapshot.valid && snapshot.state == state && snapshot.kind == kind &&
            snapshot.side == side) {
            return &snapshot;
        }
        if (!snapshot.valid && reusable == nullptr) reusable = &snapshot;
        if (snapshot.valid && reusable == nullptr &&
            (!dm::IsReadableRange(reinterpret_cast<void*>(snapshot.state),
                                  sizeof(std::uintptr_t)) ||
             *reinterpret_cast<const std::uintptr_t*>(snapshot.state) !=
                 snapshot.vtable)) {
            snapshot = {};
            reusable = &snapshot;
        }
    }
    return reusable;
}

RelotterySnapshot* FindRelotterySnapshotSlot(std::uintptr_t state,
                                             std::uintptr_t node,
                                             UnitSide side) {
    RelotterySnapshot* reusable = nullptr;
    for (RelotterySnapshot& snapshot : g_relottery_snapshots) {
        if (snapshot.valid && snapshot.state == state && snapshot.node == node &&
            snapshot.side == side) {
            return &snapshot;
        }
        if (!snapshot.valid && reusable == nullptr) reusable = &snapshot;
        if (snapshot.valid && reusable == nullptr &&
            !IsRelotteryNodeInState(snapshot.state, snapshot.node)) {
            snapshot = {};
            reusable = &snapshot;
        }
    }
    return reusable;
}

const char* StateName(StateKind kind) {
    switch (kind) {
        case StateKind::Wait: return "pausa tatica";
        case StateKind::AttackWait: return "espera de ataque";
        case StateKind::Searching: return "busca";
    }
    return "estado";
}

const char* ScalarName(ScalarKind kind) {
    switch (kind) {
        case ScalarKind::Movement: return "movimento";
        case ScalarKind::Rush: return "investida";
        case ScalarKind::SearchRange: return "alcance de busca";
    }
    return "valor";
}

std::atomic<bool>& SampleFlag(StateKind kind, UnitSide side) {
    if (side == UnitSide::Companion) {
        switch (kind) {
            case StateKind::Wait: return g_companion_wait_sample_logged;
            case StateKind::AttackWait:
                return g_companion_attack_sample_logged;
            case StateKind::Searching:
                return g_companion_search_sample_logged;
        }
    } else {
        switch (kind) {
            case StateKind::Wait: return g_enemy_wait_sample_logged;
            case StateKind::AttackWait: return g_enemy_attack_sample_logged;
            case StateKind::Searching: return g_enemy_search_sample_logged;
        }
    }
    return g_invalid_state_logged;
}

void IncrementCounter(StateKind kind, UnitSide side) {
    if (side == UnitSide::Companion) {
        switch (kind) {
            case StateKind::Wait:
                g_companion_tactical_wait_entries.fetch_add(
                    1, std::memory_order_relaxed);
                break;
            case StateKind::AttackWait:
                g_companion_attack_wait_entries.fetch_add(
                    1, std::memory_order_relaxed);
                break;
            case StateKind::Searching:
                g_companion_search_entries.fetch_add(
                    1, std::memory_order_relaxed);
                break;
        }
    } else {
        switch (kind) {
            case StateKind::Wait:
                g_enemy_tactical_wait_entries.fetch_add(
                    1, std::memory_order_relaxed);
                break;
            case StateKind::AttackWait:
                g_enemy_attack_wait_entries.fetch_add(
                    1, std::memory_order_relaxed);
                break;
            case StateKind::Searching:
                g_enemy_search_entries.fetch_add(
                    1, std::memory_order_relaxed);
                break;
        }
    }
}

void LogSample(StateKind kind,
               UnitSide side,
               const std::array<std::uint64_t, kMaximumFields>& original,
               const std::array<std::uint64_t, kMaximumFields>& applied,
               std::size_t count) {
    if (SampleFlag(kind, side).exchange(true, std::memory_order_acq_rel)) return;
    char message[256] = {};
    std::snprintf(message, sizeof(message),
                  "Amostra de %s para %s: duracao principal %llu -> %llu (%zu campos).",
                  StateName(kind), SideName(side),
                  static_cast<unsigned long long>(original[0]),
                  static_cast<unsigned long long>(applied[0]), count);
    Log(message);
}

void ApplyStateProfile(void* raw_state, StateKind kind) {
    if (!g_enabled.load(std::memory_order_acquire) ||
        g_shutting_down.load(std::memory_order_acquire)) {
        return;
    }

    const auto state = reinterpret_cast<std::uintptr_t>(raw_state);
    std::array<std::uint64_t, kMaximumFields> original{};
    std::size_t count = 0;
    if (!ReadDurations(state, kind, original, count)) {
        g_invalid_state_logged.store(true, std::memory_order_release);
        FailClosed("Falha estrutural em um estado da IA; perfil interrompido e valores restaurados.");
        return;
    }

    UnitSide side = UnitSide::Enemy;
    if (!ResolveStateSide(state, side)) {
        IdentityFailClosed(
            "Unidade da IA sem identificacao de equipe; perfil interrompido e valores restaurados.");
        return;
    }
    if (!IsProfileEnabled(side)) return;

    std::array<std::uint64_t, kMaximumFields> applied = original;
    const LONG multiplier = CurrentDurationPercent(kind, side);
    for (std::size_t index = 0; index < count; ++index) {
        applied[index] = ScaleDuration(original[index], multiplier);
    }

    AcquireSRWLockExclusive(&g_state_lock);
    if (!g_enabled.load(std::memory_order_acquire) ||
        g_runtime_faulted.load(std::memory_order_acquire)) {
        ReleaseSRWLockExclusive(&g_state_lock);
        return;
    }
    StateSnapshot* snapshot = FindSnapshotSlot(state, kind, side);
    if (snapshot == nullptr) {
        RestoreSnapshotsLocked();
        g_runtime_faulted.store(true, std::memory_order_release);
        g_enabled.store(false, std::memory_order_release);
        ReleaseSRWLockExclusive(&g_state_lock);
        if (!g_snapshot_capacity_logged.exchange(true, std::memory_order_acq_rel)) {
            Log("Limite de estados da IA atingido; perfil interrompido e valores restaurados.");
        }
        return;
    }
    if (snapshot->valid && snapshot->field_count == count &&
        snapshot->applied == original) {
        original = snapshot->original;
        applied = original;
        for (std::size_t index = 0; index < count; ++index) {
            applied[index] = ScaleDuration(original[index], multiplier);
        }
    }
    snapshot->state = state;
    snapshot->kind = kind;
    snapshot->side = side;
    snapshot->field_count = count;
    snapshot->original = original;
    snapshot->applied = applied;
    snapshot->valid = true;
    WriteDurations(state, kind, applied, count);
    ReleaseSRWLockExclusive(&g_state_lock);

    IncrementCounter(kind, side);
    LogSample(kind, side, original, applied, count);
}

void LogScalarSample(ScalarKind kind, UnitSide side, std::uint32_t original_raw,
                     std::uint32_t applied_raw) {
    std::atomic<bool>* flag = nullptr;
    if (kind == ScalarKind::SearchRange) {
        flag = side == UnitSide::Companion
                   ? &g_companion_search_range_sample_logged
                   : &g_enemy_search_range_sample_logged;
    } else if (kind == ScalarKind::Movement) {
        flag = side == UnitSide::Companion
                   ? &g_companion_movement_sample_logged
                   : &g_enemy_movement_sample_logged;
    }
    if (flag == nullptr || flag->exchange(true, std::memory_order_acq_rel)) return;

    float original = 0.0F;
    float applied = 0.0F;
    std::memcpy(&original, &original_raw, sizeof(original));
    std::memcpy(&applied, &applied_raw, sizeof(applied));
    char message[256] = {};
    std::snprintf(message, sizeof(message),
                  "Amostra de %s para %s: %.3f -> %.3f.",
                  ScalarName(kind), SideName(side), original, applied);
    Log(message);
}

void ApplyScalarProfile(void* raw_state, ScalarKind kind) {
    if (!g_enabled.load(std::memory_order_acquire) ||
        g_shutting_down.load(std::memory_order_acquire)) {
        return;
    }

    const auto state = reinterpret_cast<std::uintptr_t>(raw_state);
    std::array<std::uint32_t, 3> original{};
    std::size_t count = 0;
    std::uintptr_t vtable = 0;
    if (!ReadScalars(state, kind, original, count, vtable)) {
        FailClosed("Falha estrutural em um parametro da IA; perfil interrompido e valores restaurados.");
        return;
    }

    UnitSide side = UnitSide::Enemy;
    if (!ResolveStateSide(state, side)) {
        IdentityFailClosed(
            "Unidade da IA sem identificacao de equipe; perfil interrompido e valores restaurados.");
        return;
    }
    if (!IsProfileEnabled(side)) return;

    std::array<std::uint32_t, 3> applied = original;
    const LONG percent = CurrentScalarPercent(kind, side);
    for (std::size_t index = 0; index < count; ++index) {
        if (!ScaleScalar(original[index], kind, percent, applied[index])) {
            FailClosed("Parametro da IA fora do limite; perfil interrompido e valores restaurados.");
            return;
        }
    }

    AcquireSRWLockExclusive(&g_state_lock);
    if (!g_enabled.load(std::memory_order_acquire) ||
        g_runtime_faulted.load(std::memory_order_acquire)) {
        ReleaseSRWLockExclusive(&g_state_lock);
        return;
    }
    ScalarSnapshot* snapshot = FindScalarSnapshotSlot(state, kind, side);
    if (snapshot == nullptr) {
        RestoreSnapshotsLocked();
        g_runtime_faulted.store(true, std::memory_order_release);
        g_enabled.store(false, std::memory_order_release);
        ReleaseSRWLockExclusive(&g_state_lock);
        if (!g_snapshot_capacity_logged.exchange(true, std::memory_order_acq_rel)) {
            Log("Limite de parametros da IA atingido; perfil interrompido e valores restaurados.");
        }
        return;
    }
    if (snapshot->valid && snapshot->vtable == vtable &&
        snapshot->field_count == count && snapshot->applied == original) {
        original = snapshot->original;
        applied = original;
        for (std::size_t index = 0; index < count; ++index) {
            if (!ScaleScalar(original[index], kind, percent, applied[index])) {
                RestoreSnapshotsLocked();
                g_runtime_faulted.store(true, std::memory_order_release);
                g_enabled.store(false, std::memory_order_release);
                ReleaseSRWLockExclusive(&g_state_lock);
                Log("Falha ao recalcular um parametro da IA; perfil interrompido e valores restaurados.");
                return;
            }
        }
    }
    snapshot->state = state;
    snapshot->vtable = vtable;
    snapshot->kind = kind;
    snapshot->side = side;
    snapshot->field_count = count;
    snapshot->original = original;
    snapshot->applied = applied;
    snapshot->valid = true;
    WriteScalars(state, kind, applied, count);
    ReleaseSRWLockExclusive(&g_state_lock);

    LogScalarSample(kind, side, original[0], applied[0]);
}

void ApplyRelotteryProfile(void* raw_state) {
    if (!g_enabled.load(std::memory_order_acquire) ||
        g_shutting_down.load(std::memory_order_acquire)) {
        return;
    }
    const auto state = reinterpret_cast<std::uintptr_t>(raw_state);
    UnitSide side = UnitSide::Enemy;
    if (!ResolveTacticalStateSide(state, side)) {
        IdentityFailClosed(
            "Estado tatico sem identificacao de equipe; perfil interrompido e valores restaurados.");
        return;
    }
    if (!IsProfileEnabled(side)) return;
    if (!dm::IsReadableRange(reinterpret_cast<const void*>(state), 0xB8)) {
        FailClosed("Estado tatico incompleto; perfil interrompido e valores restaurados.");
        return;
    }
    const std::uintptr_t sentinel =
        *reinterpret_cast<const std::uintptr_t*>(state + 0xB0);
    if (!dm::IsReadableRange(reinterpret_cast<const void*>(sentinel), 0x10)) {
        FailClosed("Lista de reavaliacao da IA invalida; perfil interrompido e valores restaurados.");
        return;
    }

    const LONG frequency = CurrentRedecisionFrequency(side);
    std::uintptr_t node = *reinterpret_cast<const std::uintptr_t*>(sentinel);
    std::size_t visited = 0;
    for (; node != sentinel && visited < kMaximumRelotteryNodesPerState;
         ++visited) {
        if (!dm::IsReadableRange(reinterpret_cast<const void*>(node), 0x48)) {
            FailClosed("No de reavaliacao da IA invalido; perfil interrompido e valores restaurados.");
            return;
        }
        const std::uintptr_t next =
            *reinterpret_cast<const std::uintptr_t*>(node);
        const std::uint32_t mode =
            *reinterpret_cast<const std::uint32_t*>(node + 0x14);
        if ((mode == 1 || mode == 3) &&
            *reinterpret_cast<const std::uint8_t*>(node + 0x44) != 0) {
            std::uint64_t current = 0;
            if (!ReadRelotteryDuration(state, node, current)) {
                FailClosed("Temporizador de reavaliacao invalido; perfil interrompido e valores restaurados.");
                return;
            }

            AcquireSRWLockExclusive(&g_state_lock);
            RelotterySnapshot* snapshot =
                FindRelotterySnapshotSlot(state, node, side);
            if (snapshot == nullptr) {
                RestoreSnapshotsLocked();
                g_runtime_faulted.store(true, std::memory_order_release);
                g_enabled.store(false, std::memory_order_release);
                ReleaseSRWLockExclusive(&g_state_lock);
                if (!g_snapshot_capacity_logged.exchange(
                        true, std::memory_order_acq_rel)) {
                    Log("Limite de temporizadores taticos atingido; perfil interrompido e valores restaurados.");
                }
                return;
            }
            if (!snapshot->valid) {
                snapshot->state = state;
                snapshot->node = node;
                snapshot->side = side;
                snapshot->original = current;
                snapshot->valid = true;
            }
            snapshot->applied = ScaleDurationByFrequency(snapshot->original,
                                                         frequency);
            WriteRelotteryDuration(node, snapshot->applied);
            const std::uint64_t original = snapshot->original;
            const std::uint64_t applied = snapshot->applied;
            ReleaseSRWLockExclusive(&g_state_lock);

            std::atomic<bool>& logged =
                side == UnitSide::Companion
                    ? g_companion_redecision_sample_logged
                    : g_enemy_redecision_sample_logged;
            if (!logged.exchange(true, std::memory_order_acq_rel)) {
                char message[256] = {};
                std::snprintf(message, sizeof(message),
                              "Amostra de reavaliacao para %s: %llu -> %llu (%ld%% de frequencia).",
                              SideName(side),
                              static_cast<unsigned long long>(original),
                              static_cast<unsigned long long>(applied), frequency);
                Log(message);
            }
        }
        node = next;
    }
    if (node != sentinel) {
        FailClosed("Lista de reavaliacao excedeu o limite; perfil interrompido e valores restaurados.");
    }
}

void ReapplySnapshots() {
    AcquireSRWLockExclusive(&g_state_lock);
    for (StateSnapshot& snapshot : g_snapshots) {
        if (!snapshot.valid) continue;
        std::array<std::uint64_t, kMaximumFields> current{};
        std::size_t count = 0;
        if (!ReadDurations(snapshot.state, snapshot.kind, current, count) ||
            count != snapshot.field_count) {
            snapshot = {};
            continue;
        }
        snapshot.applied = snapshot.original;
        if (IsProfileEnabled(snapshot.side)) {
            const LONG multiplier =
                CurrentDurationPercent(snapshot.kind, snapshot.side);
            for (std::size_t index = 0; index < count; ++index) {
                snapshot.applied[index] =
                    ScaleDuration(snapshot.original[index], multiplier);
            }
        }
        WriteDurations(snapshot.state, snapshot.kind, snapshot.applied, count);
    }
    for (ScalarSnapshot& snapshot : g_scalar_snapshots) {
        if (!snapshot.valid) continue;
        std::array<std::uint32_t, 3> current{};
        std::size_t count = 0;
        std::uintptr_t vtable = 0;
        if (!ReadScalars(snapshot.state, snapshot.kind, current, count, vtable) ||
            count != snapshot.field_count || vtable != snapshot.vtable) {
            snapshot = {};
            continue;
        }
        snapshot.applied = snapshot.original;
        bool valid = true;
        if (IsProfileEnabled(snapshot.side)) {
            const LONG percent =
                CurrentScalarPercent(snapshot.kind, snapshot.side);
            for (std::size_t index = 0; index < count; ++index) {
                valid = valid &&
                        ScaleScalar(snapshot.original[index], snapshot.kind,
                                    percent, snapshot.applied[index]);
            }
        }
        if (!valid) {
            snapshot = {};
            continue;
        }
        WriteScalars(snapshot.state, snapshot.kind, snapshot.applied, count);
    }
    for (RelotterySnapshot& snapshot : g_relottery_snapshots) {
        if (!snapshot.valid) continue;
        std::uint64_t current = 0;
        if (!ReadRelotteryDuration(snapshot.state, snapshot.node, current)) {
            snapshot = {};
            continue;
        }
        snapshot.applied =
            IsProfileEnabled(snapshot.side)
                ? ScaleDurationByFrequency(
                      snapshot.original,
                      CurrentRedecisionFrequency(snapshot.side))
                : snapshot.original;
        WriteRelotteryDuration(snapshot.node, snapshot.applied);
    }
    ReleaseSRWLockExclusive(&g_state_lock);
}

void HookWaitEnter(void* state) {
    dm::CallGuard scope(g_state_active_calls);
    g_original_wait_enter(state);
    ApplyStateProfile(state, StateKind::Wait);
}

void HookAttackWaitEnter(void* state) {
    dm::CallGuard scope(g_state_active_calls);
    g_original_attack_wait_enter(state);
    ApplyStateProfile(state, StateKind::AttackWait);
}

void HookSearchingEnter(void* state) {
    dm::CallGuard scope(g_state_active_calls);
    g_original_searching_enter(state);
    ApplyStateProfile(state, StateKind::Searching);
}

void HookMovementSetup(void* state) {
    dm::CallGuard scope(g_state_active_calls);
    g_original_movement_setup(state);
    ApplyScalarProfile(state, ScalarKind::Movement);
}

void HookAttackRushSetup(void* state) {
    dm::CallGuard scope(g_state_active_calls);
    g_original_attack_rush_setup(state);
    ApplyScalarProfile(state, ScalarKind::Rush);
}

void HookSearchingSetup(void* state) {
    dm::CallGuard scope(g_state_active_calls);
    g_original_searching_setup(state);
    ApplyScalarProfile(state, ScalarKind::SearchRange);
}

void HookTacticalStateUpdate(void* state, const void* update_info) {
    dm::CallGuard scope(g_state_active_calls);
    ApplyRelotteryProfile(state);
    g_original_tactical_state_update(state, update_info);
}

void HookTacticsManagementUpdate(void* raw_management,
                                 const void* update_info) {
    dm::CallGuard scope(g_identity_active_calls);
    UnitSide side = UnitSide::Enemy;
    const auto management =
        reinterpret_cast<std::uintptr_t>(raw_management);
    const bool identity_ok =
        IdentifyManagementSide(__builtin_return_address(0), side) &&
        RegisterTacticsManagement(management, side);
    if (!identity_ok) {
        IdentityFailClosed(
            "Gerenciador tatico sem equipe valida; perfil interrompido e valores restaurados.");
    }
    if (identity_ok && g_enabled.load(std::memory_order_acquire) &&
        !IsAiEnabled(side)) {
        std::atomic<bool>& logged =
            side == UnitSide::Companion ? g_companion_ai_block_logged
                                        : g_enemy_ai_block_logged;
        if (!logged.exchange(true, std::memory_order_acq_rel)) {
            char message[160] = {};
            std::snprintf(message, sizeof(message),
                          "Atualizacao completa da IA de %s bloqueada.",
                          SideName(side));
            Log(message);
        }
        return;
    }
    g_original_tactics_management_update(raw_management, update_info);
}

void* HookControllerFactory(void* owner, void* output, void* tactics_data,
                            void* raw_unit) {
    dm::CallGuard scope(g_identity_active_calls);
    const FactoryOrigin origin =
        IdentifyFactoryOrigin(__builtin_return_address(0));
    const auto unit = reinterpret_cast<std::uintptr_t>(raw_unit);

    bool identity_ok = false;
    if (origin == FactoryOrigin::Enemy) {
        identity_ok = RegisterUnitIdentity(unit, UnitSide::Enemy);
    } else if (origin == FactoryOrigin::Companion) {
        identity_ok = RegisterUnitIdentity(unit, UnitSide::Companion);
    } else if (origin == FactoryOrigin::ExistingUnit) {
        identity_ok = IsRegisteredUnit(unit);
    }
    if (!identity_ok) {
        IdentityFailClosed(
            "Origem da unidade da IA nao reconhecida; separacao por equipe interrompida.");
    }
    void* result =
        g_original_controller_factory(owner, output, tactics_data, raw_unit);
    if (identity_ok && !AttachControllerIdentity(
                           unit, reinterpret_cast<std::uintptr_t>(output))) {
        IdentityFailClosed(
            "Controller da IA sem vinculo valido; separacao por equipe interrompida.");
    }
    return result;
}

void* HookExploreUnitDeletingDestructor(void* raw_unit, unsigned int flags) {
    dm::CallGuard scope(g_identity_active_calls);
    RemoveUnitIdentity(reinterpret_cast<std::uintptr_t>(raw_unit));
    return g_original_explore_unit_destructor(raw_unit, flags);
}

bool InstallHooks() {
    static const std::uint8_t wait_prologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B,
        0x41, 0x28, 0x48, 0x8B, 0xD9, 0x48, 0x83, 0x78,
    };
    static const std::uint8_t attack_wait_prologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B,
        0x41, 0x28, 0x48, 0x8B, 0xD9, 0x48, 0x83, 0x78,
    };
    static const std::uint8_t searching_prologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0x41, 0x28, 0x48, 0x8B, 0xD9, 0x48, 0x83, 0x78,
    };
    static const std::uint8_t movement_setup_prologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0x51, 0x28, 0x48, 0x8B, 0xD9, 0xC6, 0x41, 0x59,
    };
    static const std::uint8_t attack_rush_setup_prologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0xD9, 0xE8, 0x92, 0xEC, 0xFF, 0xFF, 0x80, 0x7B,
    };
    static const std::uint8_t searching_setup_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
        0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
    };
    static const std::uint8_t tactical_state_update_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57,
        0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30,
    };
    static const std::uint8_t tactics_management_update_prologue[] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x55,
        0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
    };
    static const std::uint8_t controller_factory_prologue[] = {
        0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41,
        0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x4D,
    };
    static const std::uint8_t explore_unit_destructor_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0xE8,
    };
    static const std::array<std::pair<std::uintptr_t,
                                      std::array<std::uint8_t, 5>>, 6>
        controller_calls = {{
            {0x0013A072, {0xE8, 0xD9, 0x88, 0x05, 0x00}},
            {0x0013A19C, {0xE8, 0xAF, 0x87, 0x05, 0x00}},
            {0x003CA9B5, {0xE8, 0x96, 0x7F, 0xDC, 0xFF}},
            {0x003CD48C, {0xE8, 0xBF, 0x54, 0xDC, 0xFF}},
            {0x003CF0AA, {0xE8, 0xA1, 0x38, 0xDC, 0xFF}},
            {0x003D2C4B, {0xE8, 0x00, 0xFD, 0xDB, 0xFF}},
        }};
    static const std::array<std::pair<std::uintptr_t,
                                      std::array<std::uint8_t, 5>>, 2>
        management_calls = {{
            {0x0041D2D8, {0xE8, 0xF3, 0x77, 0xD7, 0xFF}},
            {0x0041D2EE, {0xE8, 0xDD, 0x77, 0xD7, 0xFF}},
        }};

    g_wait_target = reinterpret_cast<void*>(dm::Rva(kWaitEnterRva));
    g_attack_wait_target = reinterpret_cast<void*>(dm::Rva(kAttackWaitEnterRva));
    g_searching_target = reinterpret_cast<void*>(dm::Rva(kSearchingEnterRva));
    g_movement_setup_target =
        reinterpret_cast<void*>(dm::Rva(kMovementSetupRva));
    g_attack_rush_setup_target =
        reinterpret_cast<void*>(dm::Rva(kAttackRushSetupRva));
    g_searching_setup_target =
        reinterpret_cast<void*>(dm::Rva(kSearchingSetupRva));
    g_tactical_state_update_target =
        reinterpret_cast<void*>(dm::Rva(kTacticalStateUpdateRva));
    g_tactics_management_update_target =
        reinterpret_cast<void*>(dm::Rva(kTacticsManagementUpdateRva));
    g_controller_factory_target =
        reinterpret_cast<void*>(dm::Rva(kControllerFactoryRva));
    g_explore_unit_destructor_target = reinterpret_cast<void*>(
        dm::Rva(kExploreUnitDeletingDestructorRva));
    if (!dm::MatchesPrologue(dm::Rva(kWaitEnterRva), wait_prologue,
                             sizeof(wait_prologue)) ||
        !dm::MatchesPrologue(dm::Rva(kAttackWaitEnterRva), attack_wait_prologue,
                             sizeof(attack_wait_prologue)) ||
        !dm::MatchesPrologue(dm::Rva(kSearchingEnterRva), searching_prologue,
                             sizeof(searching_prologue)) ||
        !dm::MatchesPrologue(dm::Rva(kMovementSetupRva),
                             movement_setup_prologue,
                             sizeof(movement_setup_prologue)) ||
        !dm::MatchesPrologue(dm::Rva(kAttackRushSetupRva),
                             attack_rush_setup_prologue,
                             sizeof(attack_rush_setup_prologue)) ||
        !dm::MatchesPrologue(dm::Rva(kSearchingSetupRva),
                             searching_setup_prologue,
                             sizeof(searching_setup_prologue)) ||
        !dm::MatchesPrologue(dm::Rva(kTacticalStateUpdateRva),
                             tactical_state_update_prologue,
                             sizeof(tactical_state_update_prologue)) ||
        !dm::MatchesPrologue(dm::Rva(kTacticsManagementUpdateRva),
                             tactics_management_update_prologue,
                             sizeof(tactics_management_update_prologue)) ||
        !dm::MatchesPrologue(dm::Rva(kControllerFactoryRva),
                             controller_factory_prologue,
                             sizeof(controller_factory_prologue)) ||
        !dm::MatchesPrologue(dm::Rva(kExploreUnitDeletingDestructorRva),
                             explore_unit_destructor_prologue,
                             sizeof(explore_unit_destructor_prologue))) {
        Log("Build rejeitada: estados ou identidade da IA nao correspondem.");
        return false;
    }
    for (const auto& call : controller_calls) {
        if (!dm::MatchesPrologue(dm::Rva(call.first), call.second.data(),
                                 call.second.size())) {
            Log("Build rejeitada: chamadores da fabrica da IA nao correspondem.");
            return false;
        }
    }
    for (const auto& call : management_calls) {
        if (!dm::MatchesPrologue(dm::Rva(call.first), call.second.data(),
                                 call.second.size())) {
            Log("Build rejeitada: chamadas dos gerenciadores da IA nao correspondem.");
            return false;
        }
    }

    if (!dm::CreateHook(g_wait_target, reinterpret_cast<void*>(&HookWaitEnter),
                        reinterpret_cast<void**>(&g_original_wait_enter)) ||
        !dm::CreateHook(g_attack_wait_target,
                        reinterpret_cast<void*>(&HookAttackWaitEnter),
                        reinterpret_cast<void**>(&g_original_attack_wait_enter)) ||
        !dm::CreateHook(g_searching_target,
                        reinterpret_cast<void*>(&HookSearchingEnter),
                        reinterpret_cast<void**>(&g_original_searching_enter)) ||
        !dm::CreateHook(g_movement_setup_target,
                        reinterpret_cast<void*>(&HookMovementSetup),
                        reinterpret_cast<void**>(&g_original_movement_setup)) ||
        !dm::CreateHook(
            g_attack_rush_setup_target,
            reinterpret_cast<void*>(&HookAttackRushSetup),
            reinterpret_cast<void**>(&g_original_attack_rush_setup)) ||
        !dm::CreateHook(g_searching_setup_target,
                        reinterpret_cast<void*>(&HookSearchingSetup),
                        reinterpret_cast<void**>(&g_original_searching_setup)) ||
        !dm::CreateHook(
            g_tactical_state_update_target,
            reinterpret_cast<void*>(&HookTacticalStateUpdate),
            reinterpret_cast<void**>(&g_original_tactical_state_update)) ||
        !dm::CreateHook(
            g_tactics_management_update_target,
            reinterpret_cast<void*>(&HookTacticsManagementUpdate),
            reinterpret_cast<void**>(&g_original_tactics_management_update)) ||
        !dm::CreateHook(g_controller_factory_target,
                        reinterpret_cast<void*>(&HookControllerFactory),
                        reinterpret_cast<void**>(&g_original_controller_factory)) ||
        !dm::CreateHook(
            g_explore_unit_destructor_target,
            reinterpret_cast<void*>(&HookExploreUnitDeletingDestructor),
            reinterpret_cast<void**>(&g_original_explore_unit_destructor))) {
        dm::RemoveHook(g_wait_target);
        dm::RemoveHook(g_attack_wait_target);
        dm::RemoveHook(g_searching_target);
        dm::RemoveHook(g_movement_setup_target);
        dm::RemoveHook(g_attack_rush_setup_target);
        dm::RemoveHook(g_searching_setup_target);
        dm::RemoveHook(g_tactical_state_update_target);
        dm::RemoveHook(g_tactics_management_update_target);
        dm::RemoveHook(g_controller_factory_target);
        dm::RemoveHook(g_explore_unit_destructor_target);
        g_wait_target = nullptr;
        g_attack_wait_target = nullptr;
        g_searching_target = nullptr;
        g_movement_setup_target = nullptr;
        g_attack_rush_setup_target = nullptr;
        g_searching_setup_target = nullptr;
        g_tactical_state_update_target = nullptr;
        g_tactics_management_update_target = nullptr;
        g_controller_factory_target = nullptr;
        g_explore_unit_destructor_target = nullptr;
        g_original_wait_enter = nullptr;
        g_original_attack_wait_enter = nullptr;
        g_original_searching_enter = nullptr;
        g_original_movement_setup = nullptr;
        g_original_attack_rush_setup = nullptr;
        g_original_searching_setup = nullptr;
        g_original_tactical_state_update = nullptr;
        g_original_tactics_management_update = nullptr;
        g_original_controller_factory = nullptr;
        g_original_explore_unit_destructor = nullptr;
        Log("Falha ao instalar os hooks da IA.");
        return false;
    }
    g_hooks_created = true;
    if (!dm::QueueHook(g_tactics_management_update_target, true) ||
        !dm::QueueHook(g_controller_factory_target, true) ||
        !dm::QueueHook(g_explore_unit_destructor_target, true) ||
        !dm::ApplyHooks()) {
        dm::RemoveHook(g_wait_target);
        dm::RemoveHook(g_attack_wait_target);
        dm::RemoveHook(g_searching_target);
        dm::RemoveHook(g_movement_setup_target);
        dm::RemoveHook(g_attack_rush_setup_target);
        dm::RemoveHook(g_searching_setup_target);
        dm::RemoveHook(g_tactical_state_update_target);
        dm::RemoveHook(g_tactics_management_update_target);
        dm::RemoveHook(g_controller_factory_target);
        dm::RemoveHook(g_explore_unit_destructor_target);
        g_hooks_created = false;
        g_wait_target = nullptr;
        g_attack_wait_target = nullptr;
        g_searching_target = nullptr;
        g_movement_setup_target = nullptr;
        g_attack_rush_setup_target = nullptr;
        g_searching_setup_target = nullptr;
        g_tactical_state_update_target = nullptr;
        g_tactics_management_update_target = nullptr;
        g_controller_factory_target = nullptr;
        g_explore_unit_destructor_target = nullptr;
        g_original_wait_enter = nullptr;
        g_original_attack_wait_enter = nullptr;
        g_original_searching_enter = nullptr;
        g_original_movement_setup = nullptr;
        g_original_attack_rush_setup = nullptr;
        g_original_searching_setup = nullptr;
        g_original_tactical_state_update = nullptr;
        g_original_tactics_management_update = nullptr;
        g_original_controller_factory = nullptr;
        g_original_explore_unit_destructor = nullptr;
        Log("Falha ao ativar a identificacao de equipe da IA.");
        return false;
    }
    g_identity_hooks_active.store(true, std::memory_order_release);
    return true;
}

bool SetStateHooksEnabled(bool enabled) {
    if (!g_hooks_created || g_wait_target == nullptr ||
        g_attack_wait_target == nullptr || g_searching_target == nullptr ||
        g_movement_setup_target == nullptr ||
        g_attack_rush_setup_target == nullptr ||
        g_searching_setup_target == nullptr ||
        g_tactical_state_update_target == nullptr) {
        return false;
    }
    return dm::QueueHook(g_wait_target, enabled) &&
           dm::QueueHook(g_attack_wait_target, enabled) &&
           dm::QueueHook(g_searching_target, enabled) &&
           dm::QueueHook(g_movement_setup_target, enabled) &&
           dm::QueueHook(g_attack_rush_setup_target, enabled) &&
           dm::QueueHook(g_searching_setup_target, enabled) &&
           dm::QueueHook(g_tactical_state_update_target, enabled) &&
           dm::ApplyHooks();
}

bool SetIdentityHooksEnabled(bool enabled) {
    if (!g_hooks_created || g_tactics_management_update_target == nullptr ||
        g_controller_factory_target == nullptr ||
        g_explore_unit_destructor_target == nullptr) {
        return false;
    }
    return dm::QueueHook(g_tactics_management_update_target, enabled) &&
           dm::QueueHook(g_controller_factory_target, enabled) &&
           dm::QueueHook(g_explore_unit_destructor_target, enabled) &&
           dm::ApplyHooks();
}

void LogSessionSummary() {
    char message[384] = {};
    std::snprintf(
        message, sizeof(message),
        "Sessao: inimigos %llu ataque/%llu pausa/%llu busca; parceiros %llu ataque/%llu pausa/%llu busca.",
        static_cast<unsigned long long>(
            g_enemy_attack_wait_entries.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            g_enemy_tactical_wait_entries.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            g_enemy_search_entries.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            g_companion_attack_wait_entries.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            g_companion_tactical_wait_entries.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            g_companion_search_entries.load(std::memory_order_relaxed)));
    Log(message);
}

void ResetSessionCounters() {
    g_enemy_attack_wait_entries.store(0, std::memory_order_relaxed);
    g_enemy_tactical_wait_entries.store(0, std::memory_order_relaxed);
    g_enemy_search_entries.store(0, std::memory_order_relaxed);
    g_companion_attack_wait_entries.store(0, std::memory_order_relaxed);
    g_companion_tactical_wait_entries.store(0, std::memory_order_relaxed);
    g_companion_search_entries.store(0, std::memory_order_relaxed);
    g_enemy_attack_sample_logged.store(false, std::memory_order_release);
    g_enemy_wait_sample_logged.store(false, std::memory_order_release);
    g_enemy_search_sample_logged.store(false, std::memory_order_release);
    g_companion_attack_sample_logged.store(false, std::memory_order_release);
    g_companion_wait_sample_logged.store(false, std::memory_order_release);
    g_companion_search_sample_logged.store(false, std::memory_order_release);
    g_enemy_movement_sample_logged.store(false, std::memory_order_release);
    g_companion_movement_sample_logged.store(false, std::memory_order_release);
    g_enemy_search_range_sample_logged.store(false, std::memory_order_release);
    g_companion_search_range_sample_logged.store(false,
                                                  std::memory_order_release);
    g_enemy_redecision_sample_logged.store(false, std::memory_order_release);
    g_companion_redecision_sample_logged.store(false,
                                                std::memory_order_release);
    g_enemy_ai_block_logged.store(false, std::memory_order_release);
    g_companion_ai_block_logged.store(false, std::memory_order_release);
    g_snapshot_capacity_logged.store(false, std::memory_order_release);
    g_invalid_state_logged.store(false, std::memory_order_release);
    g_runtime_faulted.store(false, std::memory_order_release);
}

void RemoveHooks() {
    g_enabled.store(false, std::memory_order_release);
    g_shutting_down.store(true, std::memory_order_release);
    if (g_hooks_created &&
        g_state_hooks_active.load(std::memory_order_acquire)) {
        SetStateHooksEnabled(false);
        g_state_hooks_active.store(false, std::memory_order_release);
        dm::DrainActiveCalls(g_state_active_calls);
    }
    RestoreAndClearSnapshots();
    if (g_hooks_created &&
        g_identity_hooks_active.load(std::memory_order_acquire)) {
        SetIdentityHooksEnabled(false);
        g_identity_hooks_active.store(false, std::memory_order_release);
        dm::DrainActiveCalls(g_identity_active_calls);
    }
    ClearUnitIdentities();
    if (g_hooks_created) {
        dm::RemoveHook(g_wait_target);
        dm::RemoveHook(g_attack_wait_target);
        dm::RemoveHook(g_searching_target);
        dm::RemoveHook(g_movement_setup_target);
        dm::RemoveHook(g_attack_rush_setup_target);
        dm::RemoveHook(g_searching_setup_target);
        dm::RemoveHook(g_tactical_state_update_target);
        dm::RemoveHook(g_tactics_management_update_target);
        dm::RemoveHook(g_controller_factory_target);
        dm::RemoveHook(g_explore_unit_destructor_target);
    }
    g_hooks_created = false;
    g_wait_target = nullptr;
    g_attack_wait_target = nullptr;
    g_searching_target = nullptr;
    g_movement_setup_target = nullptr;
    g_attack_rush_setup_target = nullptr;
    g_searching_setup_target = nullptr;
    g_tactical_state_update_target = nullptr;
    g_tactics_management_update_target = nullptr;
    g_controller_factory_target = nullptr;
    g_explore_unit_destructor_target = nullptr;
    g_original_wait_enter = nullptr;
    g_original_attack_wait_enter = nullptr;
    g_original_searching_enter = nullptr;
    g_original_movement_setup = nullptr;
    g_original_attack_rush_setup = nullptr;
    g_original_searching_setup = nullptr;
    g_original_tactical_state_update = nullptr;
    g_original_tactics_management_update = nullptr;
    g_original_controller_factory = nullptr;
    g_original_explore_unit_destructor = nullptr;
}

bool SetPercentOption(const DmModValue* value, float minimum, float maximum,
                      LONG& percent) {
    if (value == nullptr || value->struct_size != sizeof(DmModValue) ||
        value->type != DmOptionType::SliderFloat ||
        !std::isfinite(value->float_value) || value->float_value < minimum ||
        value->float_value > maximum) {
        return false;
    }
    percent = static_cast<LONG>(std::lround(value->float_value));
    return percent >= static_cast<LONG>(minimum) &&
           percent <= static_cast<LONG>(maximum);
}

bool SetToggleOption(const DmModValue* value, bool& enabled) {
    if (value == nullptr || value->struct_size != sizeof(DmModValue) ||
        value->type != DmOptionType::Toggle ||
        (value->bool_value != FALSE && value->bool_value != TRUE)) {
        return false;
    }
    enabled = value->bool_value != FALSE;
    return true;
}

}  // namespace

extern "C" __declspec(dllexport) std::uint32_t WINAPI Mod_GetAbiVersion() {
    return DM_MOD_LOADER_ABI_VERSION;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Initialize(
    const DmModHostContext* context) {
    if (!dm::AcceptHostContext(context, "tactical_ai", true)) return FALSE;
    Log.Bind(context->loader, "tactical_ai");
    g_shutting_down.store(false, std::memory_order_release);
    g_identity_faulted.store(false, std::memory_order_release);
    ClearUnitIdentities();
    return InstallHooks() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Enable() {
    if (g_enabled.load(std::memory_order_acquire)) return TRUE;
    if (!g_identity_hooks_active.load(std::memory_order_acquire) ||
        g_identity_faulted.load(std::memory_order_acquire)) {
        Log("Perfil recusado: identificacao de equipe da IA indisponivel.");
        return FALSE;
    }
    if (g_state_hooks_active.load(std::memory_order_acquire)) {
        if (!SetStateHooksEnabled(false)) {
            Log("Falha ao reiniciar os hooks da IA apos uma interrupcao.");
            return FALSE;
        }
        g_state_hooks_active.store(false, std::memory_order_release);
        dm::DrainActiveCalls(g_state_active_calls);
    }
    RestoreAndClearSnapshots();
    ResetSessionCounters();
    g_shutting_down.store(false, std::memory_order_release);
    if (!SetStateHooksEnabled(true)) {
        Log("Falha ao ativar os hooks da IA.");
        return FALSE;
    }
    g_state_hooks_active.store(true, std::memory_order_release);
    g_enabled.store(true, std::memory_order_release);
    Log("Perfis agressivos de inimigos e parceiros ativos.");
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Disable() {
    const bool was_enabled =
        g_enabled.exchange(false, std::memory_order_acq_rel);
    g_shutting_down.store(true, std::memory_order_release);
    if (!g_state_hooks_active.load(std::memory_order_acquire)) {
        RestoreAndClearSnapshots();
        return TRUE;
    }
    if (!SetStateHooksEnabled(false)) {
        if (was_enabled &&
            !g_runtime_faulted.load(std::memory_order_acquire)) {
            g_enabled.store(true, std::memory_order_release);
            g_shutting_down.store(false, std::memory_order_release);
        }
        Log("Falha ao desativar os hooks da IA; estado anterior preservado.");
        return FALSE;
    }
    g_state_hooks_active.store(false, std::memory_order_release);
    dm::DrainActiveCalls(g_state_active_calls);
    RestoreAndClearSnapshots();
    if (was_enabled) LogSessionSummary();
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_SetOption(
    const char* key, const DmModValue* value) {
    if (key == nullptr) return FALSE;

    bool toggle_enabled = false;
    if (std::strcmp(key, "enemy_ai_enabled") == 0) {
        if (!SetToggleOption(value, toggle_enabled)) return FALSE;
        g_enemy_ai_enabled.store(toggle_enabled, std::memory_order_release);
        g_enemy_ai_block_logged.store(false, std::memory_order_release);
        if (g_enabled.load(std::memory_order_acquire)) {
            Log(toggle_enabled ? "IA dos inimigos ativada."
                               : "IA dos inimigos desativada.");
        }
        return TRUE;
    }
    if (std::strcmp(key, "companion_ai_enabled") == 0) {
        if (!SetToggleOption(value, toggle_enabled)) return FALSE;
        g_companion_ai_enabled.store(toggle_enabled,
                                     std::memory_order_release);
        g_companion_ai_block_logged.store(false, std::memory_order_release);
        if (g_enabled.load(std::memory_order_acquire)) {
            Log(toggle_enabled ? "IA dos parceiros ativada."
                               : "IA dos parceiros desativada.");
        }
        return TRUE;
    }

    if (std::strcmp(key, "enemy_profile_enabled") == 0) {
        if (!SetToggleOption(value, toggle_enabled)) return FALSE;
        g_enemy_profile_enabled.store(toggle_enabled,
                                      std::memory_order_release);
        if (g_enabled.load(std::memory_order_acquire)) ReapplySnapshots();
        return TRUE;
    }
    if (std::strcmp(key, "companion_profile_enabled") == 0) {
        if (!SetToggleOption(value, toggle_enabled)) return FALSE;
        g_companion_profile_enabled.store(toggle_enabled,
                                          std::memory_order_release);
        if (g_enabled.load(std::memory_order_acquire)) ReapplySnapshots();
        return TRUE;
    }

    LONG percent = 0;
    std::atomic<LONG>* destination = nullptr;
    if (std::strcmp(key, "enemy_attack_interval_percent") == 0) {
        if (!SetPercentOption(value, 10.0F, 300.0F, percent)) return FALSE;
        destination = &g_enemy_attack_interval_percent;
    } else if (std::strcmp(key, "enemy_pause_duration_percent") == 0) {
        if (!SetPercentOption(value, 10.0F, 300.0F, percent)) return FALSE;
        destination = &g_enemy_pause_duration_percent;
    } else if (std::strcmp(key, "enemy_search_interval_percent") == 0) {
        if (!SetPercentOption(value, 10.0F, 300.0F, percent)) return FALSE;
        destination = &g_enemy_search_interval_percent;
    } else if (std::strcmp(key, "enemy_movement_speed_percent") == 0) {
        if (!SetPercentOption(value, 25.0F, 500.0F, percent)) return FALSE;
        destination = &g_enemy_movement_speed_percent;
    } else if (std::strcmp(key, "enemy_search_range_percent") == 0) {
        if (!SetPercentOption(value, 25.0F, 500.0F, percent)) return FALSE;
        destination = &g_enemy_search_range_percent;
    } else if (std::strcmp(key, "enemy_redecision_frequency_percent") == 0) {
        if (!SetPercentOption(value, 25.0F, 500.0F, percent)) return FALSE;
        destination = &g_enemy_redecision_frequency_percent;
    } else if (std::strcmp(key, "companion_attack_interval_percent") == 0) {
        if (!SetPercentOption(value, 10.0F, 300.0F, percent)) return FALSE;
        destination = &g_companion_attack_interval_percent;
    } else if (std::strcmp(key, "companion_pause_duration_percent") == 0) {
        if (!SetPercentOption(value, 10.0F, 300.0F, percent)) return FALSE;
        destination = &g_companion_pause_duration_percent;
    } else if (std::strcmp(key, "companion_search_interval_percent") == 0) {
        if (!SetPercentOption(value, 10.0F, 300.0F, percent)) return FALSE;
        destination = &g_companion_search_interval_percent;
    } else if (std::strcmp(key, "companion_movement_speed_percent") == 0) {
        if (!SetPercentOption(value, 25.0F, 500.0F, percent)) return FALSE;
        destination = &g_companion_movement_speed_percent;
    } else if (std::strcmp(key, "companion_search_range_percent") == 0) {
        if (!SetPercentOption(value, 25.0F, 500.0F, percent)) return FALSE;
        destination = &g_companion_search_range_percent;
    } else if (std::strcmp(key, "companion_redecision_frequency_percent") == 0) {
        if (!SetPercentOption(value, 25.0F, 500.0F, percent)) return FALSE;
        destination = &g_companion_redecision_frequency_percent;
    } else {
        return FALSE;
    }

    destination->store(percent, std::memory_order_release);
    if (g_enabled.load(std::memory_order_acquire)) ReapplySnapshots();
    return TRUE;
}

extern "C" __declspec(dllexport) void WINAPI Mod_Shutdown() {
    if (g_enabled.load(std::memory_order_acquire)) LogSessionSummary();
    RemoveHooks();
    Log.Reset();
    dm::ReleaseHost();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
