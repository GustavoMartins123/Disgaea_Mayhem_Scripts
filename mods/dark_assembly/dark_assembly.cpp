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

constexpr std::uintptr_t kVoteUpdateRva = 0x004D2150;
constexpr std::uintptr_t kVoteStateVtableRva = 0x00A59850;
constexpr std::uintptr_t kVoteTaskVtableRva = 0x00A59960;
constexpr std::size_t kTaskPointerOffset = 0x200;
constexpr std::size_t kForcePassFlagOffset = 0x298;
constexpr std::size_t kOutcomeOffset = 0x250;

using VoteUpdateFn = bool (*)(void* state, const void* update_info);

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_shutting_down{false};
std::atomic<LONG> g_active_calls{0};
void* g_hook_target = nullptr;
VoteUpdateFn g_original_vote_update = nullptr;
bool g_minhook_initialized = false;
dm::HostLog Log;

bool ResolveVoteObjects(void* state, std::uint8_t*& force_pass_flag) {
    if (!dm::IsWritableRange(state, kForcePassFlagOffset + sizeof(std::uint8_t))) {
        return false;
    }
    const auto state_address = reinterpret_cast<std::uintptr_t>(state);
    if (!dm::HasVtable(state_address, kVoteStateVtableRva)) return false;

    const auto task = *reinterpret_cast<std::uintptr_t*>(state_address + kTaskPointerOffset);
    if (!dm::IsWritableRange(reinterpret_cast<void*>(task),
                             kOutcomeOffset + sizeof(std::uint8_t)) ||
        !dm::HasVtable(task, kVoteTaskVtableRva)) {
        return false;
    }
    force_pass_flag = reinterpret_cast<std::uint8_t*>(state_address + kForcePassFlagOffset);
    return true;
}

bool HookVoteUpdate(void* state, const void* update_info) {
    dm::CallGuard scope(g_active_calls);
    std::uint8_t* force_pass_flag = nullptr;
    std::uint8_t previous_value = 0;
    const bool force_pass = g_enabled.load(std::memory_order_acquire) &&
                            ResolveVoteObjects(state, force_pass_flag);
    if (force_pass) {
        previous_value = *force_pass_flag;
        *force_pass_flag = 1;
    }

    const bool result = g_original_vote_update(state, update_info);

    if (force_pass && !g_shutting_down.load(std::memory_order_acquire)) {
        std::uint8_t* current_flag = nullptr;
        if (ResolveVoteObjects(state, current_flag) && current_flag == force_pass_flag) {
            *current_flag = previous_value;
        }
    }
    return result;
}

bool InstallHook() {
    static const std::uint8_t expected_prologue[] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x55,
        0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56
    };
    g_hook_target = reinterpret_cast<void*>(dm::Rva(kVoteUpdateRva));
    if (!dm::MatchesPrologue(dm::Rva(kVoteUpdateRva), expected_prologue,
                             sizeof(expected_prologue))) {
        Log("Build rejeitada: rotina de votacao nao corresponde.");
        g_hook_target = nullptr;
        return false;
    }

    if (MH_Initialize() != MH_OK) {
        Log("Falha ao inicializar MinHook.");
        g_hook_target = nullptr;
        return false;
    }
    g_minhook_initialized = true;
    if (MH_CreateHook(g_hook_target, reinterpret_cast<LPVOID>(&HookVoteUpdate),
                      reinterpret_cast<LPVOID*>(&g_original_vote_update)) != MH_OK) {
        MH_Uninitialize();
        g_minhook_initialized = false;
        g_hook_target = nullptr;
        g_original_vote_update = nullptr;
        Log("Falha ao instalar o hook de votacao.");
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
    g_original_vote_update = nullptr;
}

}  // namespace

extern "C" __declspec(dllexport) std::uint32_t WINAPI Mod_GetAbiVersion() {
    return DM_MOD_LOADER_ABI_VERSION;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Initialize(const DmModHostContext* context) {
    if (!dm::AcceptHostContext(context, true)) return FALSE;
    Log.Bind(context->loader, "dark_assembly");
    g_shutting_down.store(false, std::memory_order_release);
    return InstallHook() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Enable() {
    if (!g_minhook_initialized || g_hook_target == nullptr) return FALSE;
    const MH_STATUS status = MH_EnableHook(g_hook_target);
    if (status != MH_OK && status != MH_ERROR_ENABLED) {
        Log("Falha ao ativar o hook de votacao.");
        return FALSE;
    }
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
