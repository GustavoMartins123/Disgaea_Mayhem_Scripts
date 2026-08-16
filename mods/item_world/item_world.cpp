#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <algorithm>

// -----------------------------------------------------------------------------
// Global Mod State
// -----------------------------------------------------------------------------
static bool g_mod_enabled = true;
static int g_levels_per_floor = 5;
static bool g_auto_subdue = true;
static int g_mystery_room_rate = 75;
static HANDLE g_monitor_thread = NULL;
static bool g_thread_running = false;

// Memory addresses
static uintptr_t g_exe_base = 0;
static uintptr_t g_vtable_item_status = 0;
static uintptr_t g_vtable_item_world = 0;

// -----------------------------------------------------------------------------
// Resident Memory Hook & Scanner Worker
// -----------------------------------------------------------------------------
static DWORD WINAPI ItemWorldMonitorThread(LPVOID lpParam) {
    g_thread_running = true;

    g_exe_base = (uintptr_t)GetModuleHandleA(NULL);
    if (!g_exe_base) g_exe_base = 0x140000000;

    g_vtable_item_status = g_exe_base + 0xA252C0;
    g_vtable_item_world = g_exe_base + 0xA251F0;

    SYSTEM_INFO sys_info = {};
    GetSystemInfo(&sys_info);
    const uintptr_t min_addr = (uintptr_t)sys_info.lpMinimumApplicationAddress;
    const uintptr_t max_addr = (uintptr_t)sys_info.lpMaximumApplicationAddress;

    while (g_thread_running) {
        if (g_mod_enabled) {
            uintptr_t address = min_addr;
            MEMORY_BASIC_INFORMATION mbi = {};

            while (address < max_addr && VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi))) {
                if (mbi.State == MEM_COMMIT &&
                    (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
                    uint8_t* buffer = (uint8_t*)mbi.BaseAddress;
                    const size_t size = mbi.RegionSize;

                    if (size >= 0x400) {
                        // 1. Monitor active CItemWorldData sessions
                        for (size_t i = 0; i <= size - 0x100; i += 8) {
                            uintptr_t vptr = *(uintptr_t*)(buffer + i);
                            if (vptr == g_vtable_item_world) {
                                uint8_t* iw_obj = buffer + i;
                                
                                // Offset +0x74: Level increment per floor
                                int32_t* p_level_inc = (int32_t*)(iw_obj + 0x74);
                                if (*p_level_inc != g_levels_per_floor && *p_level_inc >= 0 && *p_level_inc <= 100) {
                                    *p_level_inc = g_levels_per_floor;
                                }

                                // Offset +0x40: Pointer to active CItemStatus
                                uintptr_t item_ptr = *(uintptr_t*)(iw_obj + 0x40);
                                if (item_ptr >= min_addr && item_ptr < max_addr) {
                                    uint8_t* item_obj = (uint8_t*)item_ptr;
                                    if (*(uintptr_t*)item_obj == g_vtable_item_status) {
                                        // Auto-subdue innocents in active item
                                        if (g_auto_subdue) {
                                            uintptr_t inno_start = *(uintptr_t*)(item_obj + 0x358);
                                            uintptr_t inno_end = *(uintptr_t*)(item_obj + 0x360);
                                            if (inno_start && inno_end >= inno_start && (inno_end - inno_start) <= 64 * 8) {
                                                for (uintptr_t p = inno_start; p < inno_end; p += 8) {
                                                    uint8_t* inno_obj = *(uint8_t**)p;
                                                    if (inno_obj) {
                                                        uint32_t* p_subdued = (uint32_t*)(inno_obj + 0x14);
                                                        int32_t* p_power = (int32_t*)(inno_obj + 0x18);
                                                        if (*p_subdued == 0) {
                                                            *p_subdued = 1;
                                                            if (*p_power > 0) {
                                                                *p_power *= 2;
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                address = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            }
        }
        Sleep(150); // Monitor interval
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
            g_monitor_thread = CreateThread(NULL, 0, ItemWorldMonitorThread, NULL, 0, NULL);
        }
    }

    __declspec(dllexport) void Mod_Disable() {
        g_mod_enabled = false;
    }

    __declspec(dllexport) void Mod_SetOption(const char* key, int int_val, bool bool_val) {
        if (!key) return;
        if (strcmp(key, "levels_per_floor") == 0) {
            g_levels_per_floor = int_val;
        } else if (strcmp(key, "auto_subdue") == 0) {
            g_auto_subdue = bool_val;
        } else if (strcmp(key, "mystery_room_rate") == 0) {
            g_mystery_room_rate = int_val;
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
            Mod_Enable();
            break;
        case DLL_PROCESS_DETACH:
            g_thread_running = false;
            if (g_monitor_thread) {
                WaitForSingleObject(g_monitor_thread, 500);
                CloseHandle(g_monitor_thread);
                g_monitor_thread = NULL;
            }
            break;
    }
    return TRUE;
}
