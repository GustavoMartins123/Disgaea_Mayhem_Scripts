#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// -----------------------------------------------------------------------------
// Global Chara World Mod State (Volatile / Atomic)
// -----------------------------------------------------------------------------
static volatile bool g_mod_enabled = false;
static volatile int g_target_energy = 100;
static volatile bool g_freeze_energy = true;
static HANDLE g_monitor_thread = NULL;
static volatile bool g_thread_running = false;

// Dynamic VTable addresses (calculated with ASLR Base)
static uintptr_t g_exe_base = 0;
static uintptr_t g_vtable_cw_info = 0;
static uintptr_t g_vtable_cw_energy_ui = 0;

// Cached object pointers
static uintptr_t g_cached_cw_info = 0;
static uintptr_t g_cached_cw_ui = 0;

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
// Resident Memory Hook & Continuous Energy Monitor Thread
// -----------------------------------------------------------------------------
static DWORD WINAPI CharaWorldMonitorThread(LPVOID lpParam) {
    g_thread_running = true;

    g_exe_base = (uintptr_t)GetModuleHandleA(NULL);
    if (!g_exe_base) g_exe_base = 0x140000000;

    g_vtable_cw_info = g_exe_base + 0xA57610;
    g_vtable_cw_energy_ui = g_exe_base + 0xA71728;

    int tick_count = 0;

    while (g_thread_running) {
        // Sync with enabled.txt periodically every 500ms
        if (++tick_count % 10 == 0) {
            if (!CheckEnabledTxt()) {
                g_mod_enabled = false;
            }
        }

        if (g_mod_enabled && g_freeze_energy) {
            bool info_valid = false;
            bool ui_valid = false;

            // 1. Fast Path: Use cached pointers
            if (g_cached_cw_info && IsValidReadPtr((const void*)g_cached_cw_info, 0x200)) {
                if (*(uintptr_t*)g_cached_cw_info == g_vtable_cw_info) {
                    int32_t* p_energy = (int32_t*)(g_cached_cw_info + 0x178);
                    if (*p_energy != g_target_energy) {
                        *p_energy = g_target_energy;
                    }
                    info_valid = true;
                } else {
                    g_cached_cw_info = 0;
                }
            } else {
                g_cached_cw_info = 0;
            }

            if (g_cached_cw_ui && IsValidReadPtr((const void*)g_cached_cw_ui, 0x100)) {
                if (*(uintptr_t*)g_cached_cw_ui == g_vtable_cw_energy_ui) {
                    int32_t* p_cur = (int32_t*)(g_cached_cw_ui + 0x70);
                    int32_t* p_bar = (int32_t*)(g_cached_cw_ui + 0x78);
                    int32_t* p_tgt = (int32_t*)(g_cached_cw_ui + 0x7C);
                    int32_t* p_dsp = (int32_t*)(g_cached_cw_ui + 0x80);

                    if (*p_cur != g_target_energy) {
                        *p_cur = g_target_energy;
                        *p_bar = g_target_energy;
                        *p_tgt = g_target_energy;
                        *p_dsp = g_target_energy;
                    }
                    ui_valid = true;
                } else {
                    g_cached_cw_ui = 0;
                }
            } else {
                g_cached_cw_ui = 0;
            }

            // 2. Slow Path: Locate objects in heap if not yet cached
            if (!info_valid || !ui_valid) {
                uintptr_t address = 0x0000000010000000ULL;
                const uintptr_t max_scan_addr = 0x000003FFFFFFFFFFULL;
                MEMORY_BASIC_INFORMATION mbi = {};

                while (address < max_scan_addr && VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi))) {
                    if (mbi.State == MEM_COMMIT &&
                        (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
                        uint8_t* buffer = (uint8_t*)mbi.BaseAddress;
                        const size_t size = mbi.RegionSize;

                        if (size >= 0x200 && size <= 0x20000000) {
                            for (size_t i = 0; i <= size - 0x200; i += 8) {
                                uintptr_t vptr = *(uintptr_t*)(buffer + i);
                                
                                if (!info_valid && vptr == g_vtable_cw_info) {
                                    g_cached_cw_info = (uintptr_t)(buffer + i);
                                    int32_t* p_energy = (int32_t*)(buffer + i + 0x178);
                                    *p_energy = g_target_energy;
                                    info_valid = true;
                                } else if (!ui_valid && vptr == g_vtable_cw_energy_ui) {
                                    g_cached_cw_ui = (uintptr_t)(buffer + i);
                                    int32_t* p_cur = (int32_t*)(buffer + i + 0x70);
                                    int32_t* p_bar = (int32_t*)(buffer + i + 0x78);
                                    int32_t* p_tgt = (int32_t*)(buffer + i + 0x7C);
                                    int32_t* p_dsp = (int32_t*)(buffer + i + 0x80);

                                    *p_cur = g_target_energy;
                                    *p_bar = g_target_energy;
                                    *p_tgt = g_target_energy;
                                    *p_dsp = g_target_energy;
                                    ui_valid = true;
                                }

                                if (info_valid && ui_valid) break;
                            }
                        }
                    }
                    if (info_valid && ui_valid) break;
                    address = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
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
// DLL Entry Point
// -----------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            if (CheckEnabledTxt()) {
                Mod_Enable();
            } else {
                Mod_Disable();
            }
            break;
        case DLL_PROCESS_DETACH:
            g_thread_running = false;
            g_mod_enabled = false;
            if (g_monitor_thread) {
                WaitForSingleObject(g_monitor_thread, 500);
                CloseHandle(g_monitor_thread);
                g_monitor_thread = NULL;
            }
            break;
    }
    return TRUE;
}
