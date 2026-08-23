#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "../../native/mod_menu_overlay/vendor/minhook/include/MinHook.h"
#include "../../native/mod_loader/mod_loader_api.h"

// -----------------------------------------------------------------------------
// Global Chara World Mod State
// -----------------------------------------------------------------------------
static volatile bool g_mod_enabled = false;
static volatile int g_target_energy = 100;
static volatile bool g_freeze_energy = true;
static HANDLE g_monitor_thread = NULL;
static volatile bool g_thread_running = false;

// Dynamic Base & Cached instance pointer
static uintptr_t g_exe_base = 0;
static volatile uintptr_t g_cw_info_instance = 0;
static void* g_hook_target = nullptr;
static bool g_minhook_initialized = false;

// -----------------------------------------------------------------------------
// Safe Memory Validation
// -----------------------------------------------------------------------------
static inline bool IsValidMemoryRange(uintptr_t addr, size_t size) {
    if (addr < 0x10000 || addr > 0x7FFFFFFEFFFF) return false;
    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) && !(mbi.Protect & PAGE_NOACCESS)) {
            if ((mbi.Protect & PAGE_READWRITE) || (mbi.Protect & PAGE_EXECUTE_READWRITE)) {
                return (addr + size <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize);
            }
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// MinHook on CCharacterWorldInformation Constructor (RVA 0x004501D0)
// -----------------------------------------------------------------------------
typedef void* (*pfn_CWInfo_Ctor)(void* pThis, void* constructor_arg, void* allocator_arg);
static pfn_CWInfo_Ctor o_CWInfo_Ctor = nullptr;

static void* Hook_CWInfo_Ctor(void* pThis, void* constructor_arg, void* allocator_arg) {
    void* result = o_CWInfo_Ctor(pThis, constructor_arg, allocator_arg);
    if (result && IsValidMemoryRange((uintptr_t)result, sizeof(uintptr_t)) &&
        *(uintptr_t*)result == g_exe_base + 0x00A57610) {
        g_cw_info_instance = (uintptr_t)result;
        // Lock MaxEnergy (+0x174) and CurrentEnergy (+0x178) to target
        if (g_mod_enabled && IsValidMemoryRange((uintptr_t)result + 0x174, 8)) {
            *(int32_t*)((uintptr_t)result + 0x174) = g_target_energy;
            *(int32_t*)((uintptr_t)result + 0x178) = g_target_energy;
        }
    }
    return result;
}

// -----------------------------------------------------------------------------
// Dedicated Energy Guardian Thread (Runs only when in Chara World)
// -----------------------------------------------------------------------------
static DWORD WINAPI CharaWorldMonitorThread(LPVOID lpParam) {
    (void)lpParam;
    g_thread_running = true;

    while (g_thread_running) {
        if (g_mod_enabled && g_freeze_energy) {
            if (g_cw_info_instance && IsValidMemoryRange(g_cw_info_instance + 0x174, 8)) {
                int32_t* p_max = (int32_t*)(g_cw_info_instance + 0x174);
                int32_t* p_cur = (int32_t*)(g_cw_info_instance + 0x178);

                if (*p_max == 100 || *p_max == g_target_energy) {
                    if (*p_cur != g_target_energy) {
                        *p_cur = g_target_energy;
                    }
                } else {
                    g_cw_info_instance = 0;
                }
            }
        }

        Sleep(50);
    }

    return 0;
}

// -----------------------------------------------------------------------------
// Mod Plugin Interface Exports (ABI v1)
// -----------------------------------------------------------------------------
extern "C" {
    __declspec(dllexport) uint32_t WINAPI Mod_GetAbiVersion() {
        return DM_MOD_LOADER_ABI_VERSION;
    }

    __declspec(dllexport) BOOL WINAPI Mod_Initialize(const DmModHostContext* context) {
        if (context == NULL || context->struct_size != sizeof(DmModHostContext) ||
            context->abi_version != DM_MOD_LOADER_ABI_VERSION || context->loader == NULL) {
            return FALSE;
        }
        g_exe_base = (uintptr_t)GetModuleHandleA(NULL);
        if (g_exe_base == 0) return FALSE;

        static const uint8_t expected_prologue[] = {
            0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
            0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57
        };
        g_hook_target = (void*)(g_exe_base + 0x004501D0);
        if (memcmp(g_hook_target, expected_prologue, sizeof(expected_prologue)) != 0) {
            if (context->loader->Log != NULL) {
                context->loader->Log("chara_world", "Build do jogo rejeitada: prologo do construtor nao corresponde.");
            }
            g_hook_target = nullptr;
            return FALSE;
        }

        if (MH_Initialize() != MH_OK) return FALSE;
        g_minhook_initialized = true;
        if (MH_CreateHook(g_hook_target, (LPVOID)&Hook_CWInfo_Ctor,
                          reinterpret_cast<LPVOID*>(&o_CWInfo_Ctor)) != MH_OK ||
            MH_EnableHook(g_hook_target) != MH_OK) {
            MH_RemoveHook(g_hook_target);
            MH_Uninitialize();
            g_minhook_initialized = false;
            g_hook_target = nullptr;
            return FALSE;
        }
        return TRUE;
    }

    __declspec(dllexport) BOOL WINAPI Mod_Enable() {
        g_mod_enabled = true;
        if (!g_thread_running) {
            g_monitor_thread = CreateThread(NULL, 0, CharaWorldMonitorThread, NULL, 0, NULL);
            if (g_monitor_thread == NULL) {
                g_mod_enabled = false;
                return FALSE;
            }
        }
        return TRUE;
    }

    __declspec(dllexport) BOOL WINAPI Mod_Disable() {
        g_mod_enabled = false;
        g_cw_info_instance = 0;
        return TRUE;
    }

    __declspec(dllexport) BOOL WINAPI Mod_SetOption(const char* key, const DmModValue* value) {
        if (key == NULL || value == NULL || value->struct_size != sizeof(DmModValue)) return FALSE;
        if (strcmp(key, "locked_energy") == 0) {
            if (value->type != DmOptionType::SliderInt) return FALSE;
            g_target_energy = value->int_value;
        } else if (strcmp(key, "freeze_energy") == 0) {
            if (value->type != DmOptionType::Toggle) return FALSE;
            g_freeze_energy = value->bool_value != FALSE;
        } else {
            return FALSE;
        }
        return TRUE;
    }

    __declspec(dllexport) void WINAPI Mod_Shutdown() {
        g_mod_enabled = false;
        g_thread_running = false;
        g_cw_info_instance = 0;
        if (g_monitor_thread != NULL) {
            WaitForSingleObject(g_monitor_thread, 500);
            CloseHandle(g_monitor_thread);
            g_monitor_thread = NULL;
        }
        if (g_minhook_initialized) {
            if (g_hook_target != nullptr) {
                MH_DisableHook(g_hook_target);
                MH_RemoveHook(g_hook_target);
            }
            MH_Uninitialize();
            g_minhook_initialized = false;
            g_hook_target = nullptr;
        }
    }
}

// -----------------------------------------------------------------------------
// DLL Entry Point
// -----------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(hinstDLL);
    return TRUE;
}
