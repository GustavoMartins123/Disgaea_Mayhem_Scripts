#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "../../native/mod_menu_overlay/vendor/minhook/include/MinHook.h"

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
static volatile uintptr_t g_cached_item_world = 0;

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
// Safe Memory Access Helpers
// -----------------------------------------------------------------------------
static inline bool IsValidMemoryRange(uintptr_t addr, size_t size, DWORD required_protect) {
    if (addr < 0x10000 || addr > 0x7FFFFFFEFFFF) return false;
    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) && !(mbi.Protect & PAGE_NOACCESS)) {
            if (required_protect == PAGE_READWRITE) {
                if ((mbi.Protect & PAGE_READWRITE) || (mbi.Protect & PAGE_EXECUTE_READWRITE)) {
                    return (addr + size <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize);
                }
            } else {
                return (addr + size <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize);
            }
        }
    }
    return false;
}

static inline bool SafeWrite32(uintptr_t addr, int32_t val) {
    if (IsValidMemoryRange(addr, sizeof(int32_t), PAGE_READWRITE)) {
        *(int32_t*)addr = val;
        return true;
    }
    return false;
}

static inline bool SafeReadPtr(uintptr_t addr, uintptr_t* out_val) {
    if (IsValidMemoryRange(addr, sizeof(uintptr_t), PAGE_READONLY)) {
        *out_val = *(uintptr_t*)addr;
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Safe Monitor Thread
// -----------------------------------------------------------------------------
static DWORD WINAPI ItemWorldMonitorThread(LPVOID lpParam) {
    g_thread_running = true;

    int tick_count = 0;

    while (g_thread_running) {
        if (++tick_count % 20 == 0) {
            if (!CheckEnabledTxt()) {
                g_mod_enabled = false;
            }
        }

        if (g_mod_enabled && g_cached_item_world) {
            uintptr_t vptr = 0;
            if (SafeReadPtr(g_cached_item_world, &vptr) && vptr == g_vtable_item_world) {
                // Maintain level multiplier at +0x74
                SafeWrite32(g_cached_item_world + 0x74, g_levels_per_floor);

                // Auto Subdue Innocents
                if (g_auto_subdue) {
                    uintptr_t item_ptr = 0;
                    if (SafeReadPtr(g_cached_item_world + 0x40, &item_ptr) && item_ptr) {
                        uintptr_t item_vptr = 0;
                        if (SafeReadPtr(item_ptr, &item_vptr) && item_vptr == g_vtable_item_status) {
                            uintptr_t inno_start = 0, inno_end = 0;
                            SafeReadPtr(item_ptr + 0x358, &inno_start);
                            SafeReadPtr(item_ptr + 0x360, &inno_end);
                            if (inno_start && inno_end >= inno_start && (inno_end - inno_start) <= 64 * 8) {
                                for (uintptr_t inno_p = inno_start; inno_p < inno_end; inno_p += 8) {
                                    uintptr_t inno_obj = 0;
                                    if (SafeReadPtr(inno_p, &inno_obj) && inno_obj) {
                                        SafeWrite32(inno_obj + 0x14, 1); // Subdued = true
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
                g_cached_item_world = 0;
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
        g_cached_item_world = 0;
    }

    __declspec(dllexport) void* start_mod() {
        Mod_Enable();
        return (void*)1;
    }

    __declspec(dllexport) void uninstall_mod(void*) {
        Mod_Disable();
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
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hinstDLL);

            g_exe_base = (uintptr_t)GetModuleHandleA(NULL);
            if (!g_exe_base) g_exe_base = 0x140000000;

            g_vtable_item_status = g_exe_base + 0xA252C0;
            g_vtable_item_world = g_exe_base + 0xA251F0;

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
            if (g_monitor_thread) {
                WaitForSingleObject(g_monitor_thread, 200);
                CloseHandle(g_monitor_thread);
                g_monitor_thread = NULL;
            }
            break;
    }
    return TRUE;
}
