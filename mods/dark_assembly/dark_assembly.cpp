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

constexpr std::uintptr_t kVoteUpdateRva = 0x004D19E0;
constexpr std::uintptr_t kVoteStateVtableRva = 0x00A59850;
constexpr std::uintptr_t kVoteTaskVtableRva = 0x00A59960;
constexpr std::size_t kTaskPointerOffset = 0x200;
constexpr std::size_t kForcePassFlagOffset = 0x298;
constexpr std::size_t kOutcomeOffset = 0x250;
constexpr DWORD kExpectedTimestamp = 0x6A6AB373;
constexpr DWORD kExpectedImageSize = 0x00E01000;

using VoteUpdateFn = bool (*)(void* state, const void* update_info);

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_shutting_down{false};
std::atomic<LONG> g_active_calls{0};
std::uintptr_t g_exe_base = 0;
void* g_hook_target = nullptr;
VoteUpdateFn g_original_vote_update = nullptr;
bool g_minhook_initialized = false;
const DmModLoaderApi* g_loader = nullptr;

class HookScope {
public:
    HookScope() { g_active_calls.fetch_add(1, std::memory_order_acq_rel); }
    ~HookScope() { g_active_calls.fetch_sub(1, std::memory_order_acq_rel); }
};

void Log(const char* message) {
    if (g_loader != nullptr && g_loader->Log != nullptr) {
        g_loader->Log("dark_assembly", message);
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

bool ResolveVoteObjects(void* state, std::uint8_t*& force_pass_flag) {
    if (!IsAccessibleRange(state, kForcePassFlagOffset + sizeof(std::uint8_t), true)) {
        return false;
    }
    const auto state_address = reinterpret_cast<std::uintptr_t>(state);
    if (*reinterpret_cast<std::uintptr_t*>(state_address) !=
        g_exe_base + kVoteStateVtableRva) {
        return false;
    }

    const auto task = *reinterpret_cast<std::uintptr_t*>(state_address + kTaskPointerOffset);
    if (!IsAccessibleRange(reinterpret_cast<void*>(task),
                           kOutcomeOffset + sizeof(std::uint8_t), true) ||
        *reinterpret_cast<std::uintptr_t*>(task) != g_exe_base + kVoteTaskVtableRva) {
        return false;
    }
    force_pass_flag = reinterpret_cast<std::uint8_t*>(state_address + kForcePassFlagOffset);
    return true;
}

bool HookVoteUpdate(void* state, const void* update_info) {
    HookScope scope;
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
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48,
        0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x55
    };
    g_hook_target = reinterpret_cast<void*>(g_exe_base + kVoteUpdateRva);
    if (!IsAccessibleRange(g_hook_target, sizeof(expected_prologue), false) ||
        std::memcmp(g_hook_target, expected_prologue, sizeof(expected_prologue)) != 0) {
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
                      reinterpret_cast<LPVOID*>(&g_original_vote_update)) != MH_OK ||
        MH_EnableHook(g_hook_target) != MH_OK) {
        MH_DisableHook(g_hook_target);
        MH_RemoveHook(g_hook_target);
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
        while (g_active_calls.load(std::memory_order_acquire) != 0) Sleep(0);
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

extern "C" __declspec(dllexport) BOOL WINAPI Mod_SetOption(const char*, const DmModValue*) {
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
