#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "../../native/mod_menu_overlay/vendor/minhook/include/MinHook.h"
#include "../../native/mod_loader/dm_mod_common.h"

namespace {

constexpr std::uintptr_t kPopulateInformationRva = 0x001B3920;
constexpr std::uintptr_t kSerializeGaugeValueRva = 0x001B0500;
constexpr std::uintptr_t kBuildListItemRva = 0x00543610;
constexpr std::uintptr_t kCheatInformationVtableRva = 0x00A25B60;
constexpr std::uintptr_t kCheatGaugeVtableRva = 0x00A25B70;
constexpr std::uintptr_t kGaugeValueVtableRva = 0x00A1E9A0;
constexpr std::uintptr_t kListItemDataVtableRva = 0x00A67950;
constexpr std::uintptr_t kListItemRowVtableRva = 0x00A67608;
constexpr std::size_t kGaugeMapOffset = 0x68;
constexpr std::size_t kGaugeRecordWrapperOffset = 0x10;
constexpr std::size_t kGaugeValueOffset = 0x20;
constexpr std::size_t kCurrentValueOffset = 0x28;
constexpr std::size_t kLowerBoundOffset = 0x30;
constexpr std::size_t kUpperBoundOffset = 0x34;
constexpr std::size_t kRecordKindOffset = 0x3A8;
constexpr std::size_t kRecordDefaultOffset = 0x3AC;
constexpr std::size_t kRecordStepOffset = 0x3B0;
constexpr std::size_t kRecordMaximumOffset = 0x3B4;
constexpr std::size_t kListItemDataPointerOffset = 0x40;
constexpr std::size_t kListItemRecordWrapperOffset = 0x10;
constexpr std::size_t kListItemCurrentOffset = 0x18;
constexpr std::size_t kListItemUpperOffset = 0x20;
constexpr std::int32_t kActiveValue = 5000;
constexpr std::int32_t kDatabaseBaseMaximum = 500;
constexpr std::size_t kTargetCount = 5;
constexpr std::uint32_t kTargetIds[kTargetCount] = {
    10101,
    10102,
    10103,
    10104,
    10105,
};

using PopulateInformationFn = void (*)(void* information);
using SerializeGaugeValueFn = void (*)(void* value, void* serializer);
using BuildListItemFn = void* (*)(void* owner, void* output, void* record_reference,
                                  std::int32_t current);
using SerializerIsLoadingFn = bool (*)(void* serializer);

struct GaugeSnapshot {
    std::uint32_t id = 0;
    std::uintptr_t object = 0;
    std::int32_t current = 0;
    std::int32_t lower = 0;
    std::int32_t upper = 0;
    bool valid = false;
};

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_shutting_down{false};
std::atomic<LONG> g_active_calls{0};
std::atomic<bool> g_serializer_error_logged{false};
std::uintptr_t g_information = 0;
void* g_populate_target = nullptr;
void* g_serialize_target = nullptr;
void* g_list_item_target = nullptr;
PopulateInformationFn g_original_populate = nullptr;
SerializeGaugeValueFn g_original_serialize = nullptr;
BuildListItemFn g_original_build_list_item = nullptr;
GaugeSnapshot g_snapshots[kTargetCount] = {};
std::uintptr_t g_list_items[kTargetCount] = {};
SRWLOCK g_state_lock = SRWLOCK_INIT;
bool g_minhook_initialized = false;
dm::HostLog Log;

int TargetIndex(std::uint32_t id) {
    for (std::size_t index = 0; index < kTargetCount; ++index) {
        if (kTargetIds[index] == id) return static_cast<int>(index);
    }
    return -1;
}

bool ResolveTargetRecord(std::uintptr_t wrapper, std::uint32_t expected_id,
                         std::uint32_t& resolved_id) {
    if (!dm::IsReadableRange(reinterpret_cast<void*>(wrapper), sizeof(std::uintptr_t))) {
        return false;
    }
    const auto record = *reinterpret_cast<const std::uintptr_t*>(wrapper);
    if (!dm::IsReadableRange(reinterpret_cast<void*>(record),
                             kRecordMaximumOffset + sizeof(std::int32_t))) {
        return false;
    }

    resolved_id = *reinterpret_cast<const std::uint32_t*>(record);
    return (expected_id == 0 || resolved_id == expected_id) &&
           TargetIndex(resolved_id) >= 0 &&
           *reinterpret_cast<const std::uint32_t*>(record + kRecordKindOffset) == 1 &&
           *reinterpret_cast<const std::int32_t*>(record + kRecordDefaultOffset) == 100 &&
           *reinterpret_cast<const std::int32_t*>(record + kRecordStepOffset) == 90 &&
           *reinterpret_cast<const std::int32_t*>(record + kRecordMaximumOffset) ==
               kDatabaseBaseMaximum;
}

bool ResolveGauge(std::uintptr_t object, std::uint32_t expected_id,
                  std::uint32_t& resolved_id) {
    if (!dm::IsWritableRange(reinterpret_cast<void*>(object), 0x38) ||
        *reinterpret_cast<const std::uintptr_t*>(object) !=
            dm::Rva(kCheatGaugeVtableRva) ||
        *reinterpret_cast<const std::uintptr_t*>(object + kGaugeValueOffset) !=
            dm::Rva(kGaugeValueVtableRva)) {
        return false;
    }

    const auto wrapper = *reinterpret_cast<const std::uintptr_t*>(
        object + kGaugeRecordWrapperOffset);
    return ResolveTargetRecord(wrapper, expected_id, resolved_id);
}

bool ResolveGaugeFromValue(void* value, std::uintptr_t& object, std::uint32_t& id) {
    if (value == nullptr) return false;
    const auto value_address = reinterpret_cast<std::uintptr_t>(value);
    if (value_address < kGaugeValueOffset) return false;
    object = value_address - kGaugeValueOffset;
    return ResolveGauge(object, 0, id);
}

bool ResolveAllGauges(std::uintptr_t information,
                      std::uintptr_t (&objects)[kTargetCount]) {
    if (!dm::IsReadableRange(reinterpret_cast<void*>(information), 0xF0) ||
        *reinterpret_cast<const std::uintptr_t*>(information) !=
            dm::Rva(kCheatInformationVtableRva)) {
        return false;
    }

    const auto map = information + kGaugeMapOffset;
    const auto head = *reinterpret_cast<const std::uintptr_t*>(map + 0x08);
    const auto count = *reinterpret_cast<const std::size_t*>(map + 0x10);
    if (count != 7 || !dm::IsReadableRange(reinterpret_cast<void*>(head), 0x20)) {
        return false;
    }

    std::memset(objects, 0, sizeof(objects));
    auto node = *reinterpret_cast<const std::uintptr_t*>(head);
    for (std::size_t visited = 0; visited < count; ++visited) {
        if (node == head || !dm::IsReadableRange(reinterpret_cast<void*>(node), 0x20)) {
            return false;
        }
        const auto next = *reinterpret_cast<const std::uintptr_t*>(node);
        const auto key = *reinterpret_cast<const std::uint32_t*>(node + 0x10);
        const auto object = *reinterpret_cast<const std::uintptr_t*>(node + 0x18);
        const int index = TargetIndex(key);
        if (index >= 0) {
            std::uint32_t resolved_id = 0;
            if (objects[index] != 0 || !ResolveGauge(object, key, resolved_id)) return false;
            objects[index] = object;
        }
        node = next;
    }
    if (node != head) return false;
    for (std::uintptr_t object : objects) {
        if (object == 0) return false;
    }
    return true;
}

void ApplyActiveValue(std::uintptr_t object) {
    *reinterpret_cast<std::int32_t*>(object + kUpperBoundOffset) = kActiveValue;
    *reinterpret_cast<std::int32_t*>(object + kCurrentValueOffset) = kActiveValue;
}

bool ResolveListItemData(std::uintptr_t object, std::uint32_t expected_id,
                         std::uint32_t& resolved_id) {
    if (!dm::IsWritableRange(reinterpret_cast<void*>(object), 0x28) ||
        *reinterpret_cast<const std::uintptr_t*>(object) !=
            dm::Rva(kListItemDataVtableRva)) {
        return false;
    }
    const auto wrapper = *reinterpret_cast<const std::uintptr_t*>(
        object + kListItemRecordWrapperOffset);
    return ResolveTargetRecord(wrapper, expected_id, resolved_id);
}

void ClearListItems() {
    std::memset(g_list_items, 0, sizeof(g_list_items));
}

void SyncListItem(std::size_t index) {
    GaugeSnapshot& snapshot = g_snapshots[index];
    const std::uintptr_t list_item = g_list_items[index];
    std::uint32_t gauge_id = 0;
    std::uint32_t list_item_id = 0;
    if (!snapshot.valid ||
        !ResolveGauge(snapshot.object, snapshot.id, gauge_id) ||
        !ResolveListItemData(list_item, snapshot.id, list_item_id)) {
        g_list_items[index] = 0;
        return;
    }
    *reinterpret_cast<std::int32_t*>(list_item + kListItemCurrentOffset) =
        *reinterpret_cast<const std::int32_t*>(
            snapshot.object + kCurrentValueOffset);
    *reinterpret_cast<std::int32_t*>(list_item + kListItemUpperOffset) =
        *reinterpret_cast<const std::int32_t*>(
            snapshot.object + kUpperBoundOffset);
}

void SyncAllListItems() {
    for (std::size_t index = 0; index < kTargetCount; ++index) SyncListItem(index);
}

void RestoreSnapshot(GaugeSnapshot& snapshot) {
    std::uint32_t resolved_id = 0;
    if (snapshot.valid && ResolveGauge(snapshot.object, snapshot.id, resolved_id)) {
        *reinterpret_cast<std::int32_t*>(snapshot.object + kLowerBoundOffset) =
            snapshot.lower;
        *reinterpret_cast<std::int32_t*>(snapshot.object + kUpperBoundOffset) =
            snapshot.upper;
        *reinterpret_cast<std::int32_t*>(snapshot.object + kCurrentValueOffset) =
            snapshot.current;
    }
}

void ClearSnapshots() {
    for (GaugeSnapshot& snapshot : g_snapshots) snapshot = {};
}

bool CaptureAndApplyAll(std::uintptr_t information, bool replace_snapshots) {
    std::uintptr_t objects[kTargetCount] = {};
    if (!ResolveAllGauges(information, objects)) return false;

    if (replace_snapshots) ClearSnapshots();
    for (std::size_t index = 0; index < kTargetCount; ++index) {
        GaugeSnapshot& snapshot = g_snapshots[index];
        if (!snapshot.valid || snapshot.object != objects[index]) {
            snapshot.id = kTargetIds[index];
            snapshot.object = objects[index];
            snapshot.current = *reinterpret_cast<const std::int32_t*>(
                objects[index] + kCurrentValueOffset);
            snapshot.lower = *reinterpret_cast<const std::int32_t*>(
                objects[index] + kLowerBoundOffset);
            snapshot.upper = *reinterpret_cast<const std::int32_t*>(
                objects[index] + kUpperBoundOffset);
            snapshot.valid = true;
        }
    }
    for (std::uintptr_t object : objects) ApplyActiveValue(object);
    SyncAllListItems();
    return true;
}

bool QuerySerializerLoading(void* serializer, bool& loading) {
    if (!dm::IsReadableRange(serializer, sizeof(std::uintptr_t))) return false;
    const auto vtable = *reinterpret_cast<const std::uintptr_t*>(serializer);
    if (!dm::IsReadableRange(reinterpret_cast<void*>(vtable + 0x20),
                             sizeof(std::uintptr_t))) {
        return false;
    }
    const auto method = *reinterpret_cast<const std::uintptr_t*>(vtable + 0x20);
    if (!dm::IsExecutableAddress(reinterpret_cast<void*>(method))) return false;
    loading = reinterpret_cast<SerializerIsLoadingFn>(method)(serializer);
    return true;
}

void HookPopulateInformation(void* information) {
    dm::CallGuard scope(g_active_calls);
    g_original_populate(information);
    if (g_shutting_down.load(std::memory_order_acquire)) return;

    AcquireSRWLockExclusive(&g_state_lock);
    g_information = reinterpret_cast<std::uintptr_t>(information);
    ClearSnapshots();
    ClearListItems();
    if (g_enabled.load(std::memory_order_acquire) &&
        !CaptureAndApplyAll(g_information, true)) {
        g_information = 0;
        Log("Estrutura da Cheat Shop rejeitada; nenhum valor foi alterado.");
    }
    ReleaseSRWLockExclusive(&g_state_lock);
}

void HookSerializeGaugeValue(void* value, void* serializer) {
    dm::CallGuard scope(g_active_calls);
    std::uintptr_t object = 0;
    std::uint32_t id = 0;
    bool loading = false;
    const bool target = ResolveGaugeFromValue(value, object, id);
    const bool mode_known = !target || QuerySerializerLoading(serializer, loading);

    AcquireSRWLockExclusive(&g_state_lock);
    GaugeSnapshot* snapshot = nullptr;
    int target_index = -1;
    if (target) {
        target_index = TargetIndex(id);
        if (target_index >= 0) snapshot = &g_snapshots[target_index];
    }

    const bool active = target && mode_known && snapshot != nullptr &&
                        g_enabled.load(std::memory_order_acquire) &&
                        !g_shutting_down.load(std::memory_order_acquire);
    if (active && !loading && snapshot->valid && snapshot->object == object) {
        RestoreSnapshot(*snapshot);
    }

    g_original_serialize(value, serializer);

    if (active) {
        if (loading || !snapshot->valid || snapshot->object != object) {
            snapshot->id = id;
            snapshot->object = object;
            snapshot->current = *reinterpret_cast<const std::int32_t*>(
                object + kCurrentValueOffset);
            snapshot->lower = *reinterpret_cast<const std::int32_t*>(
                object + kLowerBoundOffset);
            snapshot->upper = *reinterpret_cast<const std::int32_t*>(
                object + kUpperBoundOffset);
            snapshot->valid = true;
        }
        ApplyActiveValue(object);
        SyncListItem(static_cast<std::size_t>(target_index));
    }
    ReleaseSRWLockExclusive(&g_state_lock);

    if (target && !mode_known &&
        !g_serializer_error_logged.exchange(true, std::memory_order_acq_rel)) {
        Log("Modo de leitura do save rejeitado; valor da Cheat Shop preservado.");
    }
}

void* HookBuildListItem(void* owner, void* output, void* record_reference,
                        std::int32_t current) {
    dm::CallGuard scope(g_active_calls);
    void* result = g_original_build_list_item(owner, output, record_reference, current);
    if (g_shutting_down.load(std::memory_order_acquire) ||
        !dm::IsReadableRange(output, sizeof(std::uintptr_t)) ||
        !dm::IsReadableRange(record_reference, sizeof(std::uintptr_t))) {
        return result;
    }

    const auto row = *reinterpret_cast<const std::uintptr_t*>(output);
    if (!dm::IsReadableRange(reinterpret_cast<void*>(row),
                             kListItemDataPointerOffset + sizeof(std::uintptr_t)) ||
        *reinterpret_cast<const std::uintptr_t*>(row) !=
            dm::Rva(kListItemRowVtableRva)) {
        return result;
    }
    const auto list_item = *reinterpret_cast<const std::uintptr_t*>(
        row + kListItemDataPointerOffset);
    std::uint32_t id = 0;
    if (!ResolveListItemData(list_item, 0, id) ||
        *reinterpret_cast<const std::uintptr_t*>(
            list_item + kListItemRecordWrapperOffset) !=
            *reinterpret_cast<const std::uintptr_t*>(record_reference)) {
        return result;
    }

    const int index = TargetIndex(id);
    if (index < 0) return result;
    AcquireSRWLockExclusive(&g_state_lock);
    g_list_items[index] = list_item;
    if (g_enabled.load(std::memory_order_acquire)) {
        SyncListItem(static_cast<std::size_t>(index));
    }
    ReleaseSRWLockExclusive(&g_state_lock);
    return result;
}

bool InstallHooks() {
    static const std::uint8_t populate_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
        0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
    };
    static const std::uint8_t serialize_prologue[] = {
        0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x08, 0x49,
        0x89, 0x6B, 0x10, 0x49, 0x89, 0x73, 0x18, 0x57,
    };
    static const std::uint8_t list_item_prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57,
        0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x50,
    };
    g_populate_target = reinterpret_cast<void*>(dm::Rva(kPopulateInformationRva));
    g_serialize_target = reinterpret_cast<void*>(dm::Rva(kSerializeGaugeValueRva));
    g_list_item_target = reinterpret_cast<void*>(dm::Rva(kBuildListItemRva));
    if (!dm::MatchesPrologue(dm::Rva(kPopulateInformationRva), populate_prologue,
                             sizeof(populate_prologue)) ||
        !dm::MatchesPrologue(dm::Rva(kSerializeGaugeValueRva), serialize_prologue,
                             sizeof(serialize_prologue)) ||
        !dm::MatchesPrologue(dm::Rva(kBuildListItemRva), list_item_prologue,
                             sizeof(list_item_prologue))) {
        Log("Build rejeitada: rotinas da Cheat Shop nao correspondem.");
        return false;
    }

    if (MH_Initialize() != MH_OK) {
        Log("Falha ao inicializar MinHook.");
        return false;
    }
    g_minhook_initialized = true;
    if (MH_CreateHook(g_populate_target,
                      reinterpret_cast<LPVOID>(&HookPopulateInformation),
                      reinterpret_cast<LPVOID*>(&g_original_populate)) != MH_OK ||
        MH_CreateHook(g_serialize_target,
                      reinterpret_cast<LPVOID>(&HookSerializeGaugeValue),
                      reinterpret_cast<LPVOID*>(&g_original_serialize)) != MH_OK ||
        MH_CreateHook(g_list_item_target,
                      reinterpret_cast<LPVOID>(&HookBuildListItem),
                      reinterpret_cast<LPVOID*>(&g_original_build_list_item)) != MH_OK) {
        MH_RemoveHook(g_populate_target);
        MH_RemoveHook(g_serialize_target);
        MH_RemoveHook(g_list_item_target);
        MH_Uninitialize();
        g_minhook_initialized = false;
        g_populate_target = nullptr;
        g_serialize_target = nullptr;
        g_list_item_target = nullptr;
        g_original_populate = nullptr;
        g_original_serialize = nullptr;
        g_original_build_list_item = nullptr;
        Log("Falha ao instalar os hooks da Cheat Shop.");
        return false;
    }
    return true;
}

bool SetHooksEnabled(bool enabled) {
    if (!g_minhook_initialized || g_populate_target == nullptr ||
        g_serialize_target == nullptr || g_list_item_target == nullptr) {
        return false;
    }
    const auto queue = enabled ? &MH_QueueEnableHook : &MH_QueueDisableHook;
    return queue(g_populate_target) == MH_OK && queue(g_serialize_target) == MH_OK &&
           queue(g_list_item_target) == MH_OK && MH_ApplyQueued() == MH_OK;
}

void RemoveHooks() {
    g_enabled.store(false, std::memory_order_release);
    g_shutting_down.store(true, std::memory_order_release);
    AcquireSRWLockExclusive(&g_state_lock);
    for (GaugeSnapshot& snapshot : g_snapshots) RestoreSnapshot(snapshot);
    SyncAllListItems();
    ClearSnapshots();
    ClearListItems();
    g_information = 0;
    ReleaseSRWLockExclusive(&g_state_lock);

    if (g_minhook_initialized) {
        if (g_populate_target != nullptr) MH_DisableHook(g_populate_target);
        if (g_serialize_target != nullptr) MH_DisableHook(g_serialize_target);
        if (g_list_item_target != nullptr) MH_DisableHook(g_list_item_target);
        dm::DrainActiveCalls(g_active_calls);
        if (g_populate_target != nullptr) MH_RemoveHook(g_populate_target);
        if (g_serialize_target != nullptr) MH_RemoveHook(g_serialize_target);
        if (g_list_item_target != nullptr) MH_RemoveHook(g_list_item_target);
        MH_Uninitialize();
    }
    g_minhook_initialized = false;
    g_populate_target = nullptr;
    g_serialize_target = nullptr;
    g_list_item_target = nullptr;
    g_original_populate = nullptr;
    g_original_serialize = nullptr;
    g_original_build_list_item = nullptr;
}

}  // namespace

extern "C" __declspec(dllexport) std::uint32_t WINAPI Mod_GetAbiVersion() {
    return DM_MOD_LOADER_ABI_VERSION;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Initialize(
    const DmModHostContext* context) {
    if (!dm::AcceptHostContext(context, true)) return FALSE;
    Log.Bind(context->loader, "cheat_shop");
    g_shutting_down.store(false, std::memory_order_release);
    return InstallHooks() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Enable() {
    AcquireSRWLockExclusive(&g_state_lock);
    if (g_information != 0 && !CaptureAndApplyAll(g_information, true)) {
        ReleaseSRWLockExclusive(&g_state_lock);
        Log("Ativacao rejeitada: estrutura completa da Cheat Shop nao encontrada.");
        return FALSE;
    }
    if (!SetHooksEnabled(true)) {
        for (GaugeSnapshot& snapshot : g_snapshots) RestoreSnapshot(snapshot);
        SyncAllListItems();
        ClearSnapshots();
        ReleaseSRWLockExclusive(&g_state_lock);
        Log("Falha ao ativar os hooks da Cheat Shop.");
        return FALSE;
    }
    g_enabled.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_state_lock);
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Disable() {
    AcquireSRWLockExclusive(&g_state_lock);
    g_enabled.store(false, std::memory_order_release);
    for (GaugeSnapshot& snapshot : g_snapshots) RestoreSnapshot(snapshot);
    SyncAllListItems();
    ClearSnapshots();
    ReleaseSRWLockExclusive(&g_state_lock);
    SetHooksEnabled(false);
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_SetOption(
    const char*, const DmModValue*) {
    return FALSE;
}

extern "C" __declspec(dllexport) void WINAPI Mod_Shutdown() {
    RemoveHooks();
    Log.Reset();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
