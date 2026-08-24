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
constexpr std::uintptr_t kControllerFactoryRva = 0x00192950;
constexpr std::uintptr_t kExploreUnitDeletingDestructorRva = 0x00163D60;

constexpr std::uintptr_t kWaitVtableRva = 0x00A1C7C8;
constexpr std::uintptr_t kAttackWaitVtableRva = 0x00A1C540;
constexpr std::uintptr_t kSearchingVtableRva = 0x00A1C188;
constexpr std::uintptr_t kControllerVtableRva = 0x00A1BF00;
constexpr std::uintptr_t kExploreUnitVtableRva = 0x00A1D088;

constexpr std::uintptr_t kRebuildControllerReturnRvaA = 0x0013A077;
constexpr std::uintptr_t kRebuildControllerReturnRvaB = 0x0013A1A1;
constexpr std::uintptr_t kCompanionPlayerControllerReturnRva = 0x003CA9BA;
constexpr std::uintptr_t kCompanionKidsControllerReturnRva = 0x003CD491;
constexpr std::uintptr_t kEnemyKidsControllerReturnRva = 0x003CF0AF;
constexpr std::uintptr_t kEnemyControllerReturnRva = 0x003D2C50;

constexpr LONG kMultiplierScale = 1000;
constexpr LONG kDefaultAttackWaitMultiplier = 550;
constexpr LONG kDefaultTacticalWaitMultiplier = 350;
constexpr LONG kDefaultSearchDelayMultiplier = 600;
constexpr LONG kDefaultCompanionAttackWaitMultiplier = 400;
constexpr LONG kDefaultCompanionTacticalWaitMultiplier = 200;
constexpr LONG kDefaultCompanionSearchDelayMultiplier = 400;
constexpr std::uint64_t kMaximumDuration = 3600000;
constexpr std::size_t kMaximumSnapshots = 512;
constexpr std::size_t kMaximumUnitIdentities = 1024;
constexpr std::size_t kMaximumFields = 4;

constexpr std::array<std::size_t, 4> kWaitDurationOffsets = {
    0x40, 0x48, 0x70, 0x78,
};
constexpr std::array<std::size_t, 4> kAttackWaitDurationOffsets = {
    0x70, 0x78, 0x1A8, 0x1B0,
};
constexpr std::array<std::size_t, 2> kSearchingDurationOffsets = {
    0x70, 0x78,
};

using StateEnterFn = void (*)(void* state);
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

struct StateSnapshot {
    std::uintptr_t state = 0;
    StateKind kind = StateKind::Wait;
    UnitSide side = UnitSide::Enemy;
    std::size_t field_count = 0;
    std::array<std::uint64_t, kMaximumFields> original{};
    std::array<std::uint64_t, kMaximumFields> applied{};
    bool valid = false;
};

struct UnitIdentity {
    std::uintptr_t unit = 0;
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
std::atomic<LONG> g_enemy_attack_wait_multiplier{kDefaultAttackWaitMultiplier};
std::atomic<LONG> g_enemy_tactical_wait_multiplier{kDefaultTacticalWaitMultiplier};
std::atomic<LONG> g_enemy_search_delay_multiplier{kDefaultSearchDelayMultiplier};
std::atomic<LONG> g_companion_attack_wait_multiplier{
    kDefaultCompanionAttackWaitMultiplier};
std::atomic<LONG> g_companion_tactical_wait_multiplier{
    kDefaultCompanionTacticalWaitMultiplier};
std::atomic<LONG> g_companion_search_delay_multiplier{
    kDefaultCompanionSearchDelayMultiplier};
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

void* g_wait_target = nullptr;
void* g_attack_wait_target = nullptr;
void* g_searching_target = nullptr;
void* g_controller_factory_target = nullptr;
void* g_explore_unit_destructor_target = nullptr;
StateEnterFn g_original_wait_enter = nullptr;
StateEnterFn g_original_attack_wait_enter = nullptr;
StateEnterFn g_original_searching_enter = nullptr;
ControllerFactoryFn g_original_controller_factory = nullptr;
ExploreUnitDeletingDestructorFn g_original_explore_unit_destructor = nullptr;
bool g_hooks_created = false;
SRWLOCK g_state_lock = SRWLOCK_INIT;
SRWLOCK g_identity_lock = SRWLOCK_INIT;
std::array<StateSnapshot, kMaximumSnapshots> g_snapshots{};
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

LONG CurrentMultiplier(StateKind kind, UnitSide side) {
    if (side == UnitSide::Companion) {
        switch (kind) {
            case StateKind::Wait:
                return g_companion_tactical_wait_multiplier.load(
                    std::memory_order_acquire);
            case StateKind::AttackWait:
                return g_companion_attack_wait_multiplier.load(
                    std::memory_order_acquire);
            case StateKind::Searching:
                return g_companion_search_delay_multiplier.load(
                    std::memory_order_acquire);
        }
    } else {
        switch (kind) {
            case StateKind::Wait:
                return g_enemy_tactical_wait_multiplier.load(
                    std::memory_order_acquire);
            case StateKind::AttackWait:
                return g_enemy_attack_wait_multiplier.load(
                    std::memory_order_acquire);
            case StateKind::Searching:
                return g_enemy_search_delay_multiplier.load(
                    std::memory_order_acquire);
        }
    }
    return kMultiplierScale;
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

std::uint64_t ScaleDuration(std::uint64_t value, LONG multiplier) {
    if (value == 0 || multiplier >= kMultiplierScale) return value;
    const std::uint64_t scaled =
        (value * static_cast<std::uint64_t>(multiplier) + kMultiplierScale - 1) /
        kMultiplierScale;
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

const char* StateName(StateKind kind) {
    switch (kind) {
        case StateKind::Wait: return "pausa tatica";
        case StateKind::AttackWait: return "espera de ataque";
        case StateKind::Searching: return "busca";
    }
    return "estado";
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

    std::array<std::uint64_t, kMaximumFields> applied = original;
    const LONG multiplier = CurrentMultiplier(kind, side);
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
        const LONG multiplier = CurrentMultiplier(snapshot.kind, snapshot.side);
        for (std::size_t index = 0; index < count; ++index) {
            snapshot.applied[index] =
                ScaleDuration(snapshot.original[index], multiplier);
        }
        WriteDurations(snapshot.state, snapshot.kind, snapshot.applied, count);
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

    return g_original_controller_factory(owner, output, tactics_data, raw_unit);
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

    g_wait_target = reinterpret_cast<void*>(dm::Rva(kWaitEnterRva));
    g_attack_wait_target = reinterpret_cast<void*>(dm::Rva(kAttackWaitEnterRva));
    g_searching_target = reinterpret_cast<void*>(dm::Rva(kSearchingEnterRva));
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

    if (!dm::CreateHook(g_wait_target, reinterpret_cast<void*>(&HookWaitEnter),
                        reinterpret_cast<void**>(&g_original_wait_enter)) ||
        !dm::CreateHook(g_attack_wait_target,
                        reinterpret_cast<void*>(&HookAttackWaitEnter),
                        reinterpret_cast<void**>(&g_original_attack_wait_enter)) ||
        !dm::CreateHook(g_searching_target,
                        reinterpret_cast<void*>(&HookSearchingEnter),
                        reinterpret_cast<void**>(&g_original_searching_enter)) ||
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
        dm::RemoveHook(g_controller_factory_target);
        dm::RemoveHook(g_explore_unit_destructor_target);
        g_wait_target = nullptr;
        g_attack_wait_target = nullptr;
        g_searching_target = nullptr;
        g_controller_factory_target = nullptr;
        g_explore_unit_destructor_target = nullptr;
        g_original_wait_enter = nullptr;
        g_original_attack_wait_enter = nullptr;
        g_original_searching_enter = nullptr;
        g_original_controller_factory = nullptr;
        g_original_explore_unit_destructor = nullptr;
        Log("Falha ao instalar os hooks da IA.");
        return false;
    }
    g_hooks_created = true;
    if (!dm::QueueHook(g_controller_factory_target, true) ||
        !dm::QueueHook(g_explore_unit_destructor_target, true) ||
        !dm::ApplyHooks()) {
        dm::RemoveHook(g_wait_target);
        dm::RemoveHook(g_attack_wait_target);
        dm::RemoveHook(g_searching_target);
        dm::RemoveHook(g_controller_factory_target);
        dm::RemoveHook(g_explore_unit_destructor_target);
        g_hooks_created = false;
        g_wait_target = nullptr;
        g_attack_wait_target = nullptr;
        g_searching_target = nullptr;
        g_controller_factory_target = nullptr;
        g_explore_unit_destructor_target = nullptr;
        g_original_wait_enter = nullptr;
        g_original_attack_wait_enter = nullptr;
        g_original_searching_enter = nullptr;
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
        g_attack_wait_target == nullptr || g_searching_target == nullptr) {
        return false;
    }
    return dm::QueueHook(g_wait_target, enabled) &&
           dm::QueueHook(g_attack_wait_target, enabled) &&
           dm::QueueHook(g_searching_target, enabled) && dm::ApplyHooks();
}

bool SetIdentityHooksEnabled(bool enabled) {
    if (!g_hooks_created || g_controller_factory_target == nullptr ||
        g_explore_unit_destructor_target == nullptr) {
        return false;
    }
    return dm::QueueHook(g_controller_factory_target, enabled) &&
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
        dm::RemoveHook(g_controller_factory_target);
        dm::RemoveHook(g_explore_unit_destructor_target);
    }
    g_hooks_created = false;
    g_wait_target = nullptr;
    g_attack_wait_target = nullptr;
    g_searching_target = nullptr;
    g_controller_factory_target = nullptr;
    g_explore_unit_destructor_target = nullptr;
    g_original_wait_enter = nullptr;
    g_original_attack_wait_enter = nullptr;
    g_original_searching_enter = nullptr;
    g_original_controller_factory = nullptr;
    g_original_explore_unit_destructor = nullptr;
}

bool SetMultiplierOption(const DmModValue* value, float minimum, LONG& scaled) {
    if (value == nullptr || value->struct_size != sizeof(DmModValue) ||
        value->type != DmOptionType::SliderFloat ||
        !std::isfinite(value->float_value) || value->float_value < minimum ||
        value->float_value > 1.0F) {
        return false;
    }
    scaled = static_cast<LONG>(
        std::lround(value->float_value * static_cast<float>(kMultiplierScale)));
    return scaled >= 1 && scaled <= kMultiplierScale;
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

    LONG scaled = 0;
    std::atomic<LONG>* destination = nullptr;
    if (std::strcmp(key, "enemy_attack_wait_multiplier") == 0) {
        if (!SetMultiplierOption(value, 0.25F, scaled)) return FALSE;
        destination = &g_enemy_attack_wait_multiplier;
    } else if (std::strcmp(key, "enemy_tactical_wait_multiplier") == 0) {
        if (!SetMultiplierOption(value, 0.10F, scaled)) return FALSE;
        destination = &g_enemy_tactical_wait_multiplier;
    } else if (std::strcmp(key, "enemy_search_delay_multiplier") == 0) {
        if (!SetMultiplierOption(value, 0.25F, scaled)) return FALSE;
        destination = &g_enemy_search_delay_multiplier;
    } else if (std::strcmp(key, "companion_attack_wait_multiplier") == 0) {
        if (!SetMultiplierOption(value, 0.25F, scaled)) return FALSE;
        destination = &g_companion_attack_wait_multiplier;
    } else if (std::strcmp(key, "companion_tactical_wait_multiplier") == 0) {
        if (!SetMultiplierOption(value, 0.10F, scaled)) return FALSE;
        destination = &g_companion_tactical_wait_multiplier;
    } else if (std::strcmp(key, "companion_search_delay_multiplier") == 0) {
        if (!SetMultiplierOption(value, 0.25F, scaled)) return FALSE;
        destination = &g_companion_search_delay_multiplier;
    } else {
        return FALSE;
    }

    destination->store(scaled, std::memory_order_release);
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
