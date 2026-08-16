#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <algorithm>

// -----------------------------------------------------------------------------
// Global Mod State
// -----------------------------------------------------------------------------
static volatile bool g_mod_enabled = false;
static volatile int g_levels_per_floor = 5;
static volatile bool g_auto_subdue = true;
static volatile int g_mystery_room_rate = 75;
static HANDLE g_monitor_thread = NULL;
static volatile bool g_thread_running = false;

// Memory addresses
static uintptr_t g_exe_base = 0;
static uintptr_t g_vtable_item_status = 0;
static uintptr_t g_vtable_item_world = 0;

// -----------------------------------------------------------------------------
// Read enabled.txt from mod directory
// -----------------------------------------------------------------------------
static bool CheckEnabledTxt() {
    char self_path[MAX_PATH] = {};
    HMODULE hSelf = GetModuleHandleA("item_world.dll");
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
// Resident Memory Hook & Scanner Worker
// -----------------------------------------------------------------------------
static DWORD WINAPI ItemWorldMonitorThread(LPVOID lpParam) {
    g_thread_running = true;

    g_exe_base = (uintptr_t)GetModuleHandleA(NULL);
    if (!g_exe_base) g_exe_base = 0x140000000;

    g_vtable_item_status = g_exe_base + 0xA252C0;
    g_vtable_item_world = g_exe_base + 0xA251F0;

    int tick_count = 0;

    while (g_thread_running) {
        if (++tick_count % 10 == 0) {
            if (!CheckEnabledTxt()) {
                g_mod_enabled = false;
            }
        }

        if (g_mod_enabled) {
            uintptr_t address = 0x0000000010000000ULL;
            const uintptr_t max_scan_addr = 0x000003FFFFFFFFFFULL;
            MEMORY_BASIC_INFORMATION mbi = {};

            while (address < max_scan_addr && VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi))) {
                if (mbi.State == MEM_COMMIT &&
                    (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
                    uint8_t* buffer = (uint8_t*)mbi.BaseAddress;
                    const size_t size = mbi.RegionSize;

                    if (size >= 0x400 && size <= 0x20000000) {
                        for (size_t i = 0; i <= size - 0x100; i += 8) {
                            uintptr_t vptr = *(uintptr_t*)(buffer + i);
                            if (vptr == g_vtable_item_world) {
                                uint8_t* iw_obj = buffer + i;
                                
                                int32_t* p_level_inc = (int32_t*)(iw_obj + 0x74);
                                if (*p_level_inc != g_levels_per_floor && *p_level_inc >= 0 && *p_level_inc <= 100) {
                                    *p_level_inc = g_levels_per_floor;
                                }

                                uintptr_t item_ptr = *(uintptr_t*)(iw_obj + 0x40);
                                if (item_ptr && IsValidReadPtr((const void*)item_ptr, 0x400)) {
                                    uint8_t* item_obj = (uint8_t*)item_ptr;
                                    if (*(uintptr_t*)item_obj == g_vtable_item_status) {
                                        if (g_auto_subdue) {
                                            uintptr_t inno_start = *(uintptr_t*)(item_obj + 0x358);
                                            uintptr_t inno_end = *(uintptr_t*)(item_obj + 0x360);
                                            if (inno_start && inno_end >= inno_start && (inno_end - inno_start) <= 64 * 8) {
                                                for (uintptr_t p = inno_start; p < inno_end; p += 8) {
                                                    uint8_t* inno_obj = *(uint8_t**)p;
                                                    if (inno_obj && IsValidReadPtr(inno_obj, 0x20)) {
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
        Sleep(100);
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
