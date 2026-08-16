#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "../../native/mod_menu_overlay/vendor/minhook/include/MinHook.h"

// -----------------------------------------------------------------------------
// Global Chara World Mod State
// -----------------------------------------------------------------------------
static volatile bool g_mod_enabled = false;
static volatile int g_target_energy = 100;
static volatile bool g_freeze_energy = true;
static HANDLE g_monitor_thread = NULL;
static volatile bool g_thread_running = false;

// Dynamic Base & Cached object pointers
static uintptr_t g_exe_base = 0;
static uintptr_t g_vtable_cw_info = 0;
static uintptr_t g_vtable_cw_energy_ui = 0;
static volatile uintptr_t g_cached_cw_info = 0;
static volatile uintptr_t g_cached_cw_ui = 0;

// -----------------------------------------------------------------------------
// Read enabled.txt from mod directory
// -----------------------------------------------------------------------------
static bool CheckEnabledTxt() {
    char self_path[MAX_PATH] = {};
    HMODULE hSelf = GetModuleHandleA("chara_world.dll");
    if (!hSelf) hSelf = GetModuleHandleA(NULL);
    GetModuleFileNameA(hSelf, self_path, MAX_PATH);
    char* last_slash = strrchr(self_path, '\\');
    if (last_slash) *last_slash = '\0';
    
    char enabled_path[MAX_PATH] = {};
    snprintf(enabled_path, sizeof(enabled_path), "%s\\enabled.txt", self_path);
    FILE* f = fopen(enabled_path, "r");
    if (f) {
        char ch = 0;
        fread(&ch, 1, 1, f);
        fclose(f);
        return (ch == '1');
    }
    return true;
}

// -----------------------------------------------------------------------------
// Safe Pointer Validation
// -----------------------------------------------------------------------------
static inline bool IsValidReadPtr(const void* p, size_t size = 8) {
    if (!p) return false;
    uintptr_t addr = (uintptr_t)p;
    if (addr < 0x10000 || addr > 0x7FFFFFFEFFFF) return false;
    
    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(p, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE || mbi.Protect == PAGE_READONLY)) {
            return (addr + size <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize);
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// Direct Engine Hooks via MinHook
// -----------------------------------------------------------------------------
typedef void* (*pfn_CWInfo_Alloc)(void* a1);
static pfn_CWInfo_Alloc o_CWInfo_Alloc = nullptr;

static void* Hook_CWInfo_Alloc(void* a1) {
    void* result = o_CWInfo_Alloc(a1);
    if (result && g_mod_enabled) {
        g_cached_cw_info = (uintptr_t)result;
        *(int32_t*)((uintptr_t)result + 0x174) = g_target_energy; // MaxEnergy
        *(int32_t*)((uintptr_t)result + 0x178) = g_target_energy; // CurrentEnergy
    }
    return result;
}

// -----------------------------------------------------------------------------
// Continuous Energy Lock Thread
// -----------------------------------------------------------------------------
static DWORD WINAPI CharaWorldMonitorThread(LPVOID lpParam) {
    g_thread_running = true;

    int tick_count = 0;

    while (g_thread_running) {
        if (++tick_count % 10 == 0) {
            if (!CheckEnabledTxt()) {
                g_mod_enabled = false;
            }
        }

        if (g_mod_enabled && g_freeze_energy) {
            // 1. Maintain Logic Energy in CCharacterWorldInformation
            if (g_cached_cw_info && IsValidReadPtr((const void*)g_cached_cw_info, 0x200)) {
                if (*(uintptr_t*)g_cached_cw_info == g_vtable_cw_info) {
                    int32_t* p_energy = (int32_t*)(g_cached_cw_info + 0x178);
                    if (*p_energy != g_target_energy) {
                        *p_energy = g_target_energy;
                    }
                } else {
                    g_cached_cw_info = 0;
                }
            } else {
                g_cached_cw_info = 0;
            }

            // 2. Maintain UI Display in CUIUnion_CharacterWorld_Energy
            if (g_cached_cw_ui && IsValidReadPtr((const void*)g_cached_cw_ui, 0x100)) {
                if (*(uintptr_t*)g_cached_cw_ui == g_vtable_cw_energy_ui) {
                    *(int32_t*)(g_cached_cw_ui + 0x70) = g_target_energy;
                    *(int32_t*)(g_cached_cw_ui + 0x78) = g_target_energy;
                    *(int32_t*)(g_cached_cw_ui + 0x7C) = g_target_energy;
                    *(int32_t*)(g_cached_cw_ui + 0x80) = g_target_energy;
                } else {
                    g_cached_cw_ui = 0;
                }
            } else {
                g_cached_cw_ui = 0;
            }

            // 3. Fallback scan if not captured by hook
            if (!g_cached_cw_info || !g_cached_cw_ui) {
                HANDLE heaps[64] = {};
                DWORD num_heaps = GetProcessHeaps(64, heaps);
                for (DWORD h = 0; h < num_heaps && (!g_cached_cw_info || !g_cached_cw_ui); ++h) {
                    PROCESS_HEAP_ENTRY entry = {};
                    entry.lpData = NULL;
                    while (HeapWalk(heaps[h], &entry)) {
                        if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) && entry.cbData >= 0x200 && entry.lpData) {
                            uint8_t* p = (uint8_t*)entry.lpData;
                            uintptr_t vptr = *(uintptr_t*)p;
                            if (!g_cached_cw_info && vptr == g_vtable_cw_info) {
                                g_cached_cw_info = (uintptr_t)p;
                                *(int32_t*)(p + 0x178) = g_target_energy;
                            } else if (!g_cached_cw_ui && vptr == g_vtable_cw_energy_ui) {
                                g_cached_cw_ui = (uintptr_t)p;
                                *(int32_t*)(p + 0x70) = g_target_energy;
                                *(int32_t*)(p + 0x78) = g_target_energy;
                                *(int32_t*)(p + 0x7C) = g_target_energy;
                                *(int32_t*)(p + 0x80) = g_target_energy;
                            }
                        }
                    }
                }
            }
        }
        Sleep(50);
    }

    return 0;
}

// -----------------------------------------------------------------------------
// Mod Plugin Interface Exports
// -----------------------------------------------------------------------------
extern "C" {
    __declspec(dllexport) void Mod_Enable() {
        g_mod_enabled = true;
        if (!g_thread_running) {
            g_monitor_thread = CreateThread(NULL, 0, CharaWorldMonitorThread, NULL, 0, NULL);
        }
    }

    __declspec(dllexport) void Mod_Disable() {
        g_mod_enabled = false;
        g_cached_cw_info = 0;
        g_cached_cw_ui = 0;
    }

    __declspec(dllexport) void Mod_SetOption(const char* key, int int_val, bool bool_val) {
        if (!key) return;
        if (strcmp(key, "locked_energy") == 0) {
            g_target_energy = int_val;
        } else if (strcmp(key, "freeze_energy") == 0) {
            g_freeze_energy = bool_val;
        }
    }

    __declspec(dllexport) bool Mod_IsActive() {
        return g_mod_enabled && g_thread_running;
    }
}

// -----------------------------------------------------------------------------
// DLL Entry Point & Engine Hooks Setup
// -----------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hinstDLL);
            
            g_exe_base = (uintptr_t)GetModuleHandleA(NULL);
            if (!g_exe_base) g_exe_base = 0x140000000;

            g_vtable_cw_info = g_exe_base + 0xA57610;
            g_vtable_cw_energy_ui = g_exe_base + 0xA71728;

            // Initialize MinHook for instant deterministic interception
            MH_Initialize();
            void* target_alloc = (void*)(g_exe_base + 0x4DEF80);
            MH_CreateHook(target_alloc, (LPVOID)&Hook_CWInfo_Alloc, reinterpret_cast<LPVOID*>(&o_CWInfo_Alloc));
            MH_EnableHook(target_alloc);

            if (CheckEnabledTxt()) {
                Mod_Enable();
            } else {
                Mod_Disable();
            }
            break;
        }
        case DLL_PROCESS_DETACH:
            g_thread_running = false;
            g_mod_enabled = false;
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
            if (g_monitor_thread) {
                WaitForSingleObject(g_monitor_thread, 500);
                CloseHandle(g_monitor_thread);
                g_monitor_thread = NULL;
            }
            break;
    }
    return TRUE;
}
