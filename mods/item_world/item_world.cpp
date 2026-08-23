#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "../../native/mod_loader/mod_loader_api.h"

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
    (void)lpParam;
    g_thread_running = true;

    while (g_thread_running) {
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
        g_vtable_item_status = g_exe_base + 0xA252C0;
        g_vtable_item_world = g_exe_base + 0xA251F0;

        // No constructor/factory hook currently captures CItemWorldData, so the
        // previous plugin could never populate g_cached_item_world. Reject the
        // plugin explicitly instead of reporting a resident but inert mod.
        if (context->loader->Log != NULL) {
            context->loader->Log("item_world", "Inicializacao rejeitada: captura de CItemWorldData nao implementada.");
        }
        return FALSE;
    }

    __declspec(dllexport) BOOL WINAPI Mod_Enable() {
        g_mod_enabled = true;
        if (!g_thread_running) {
            g_monitor_thread = CreateThread(NULL, 0, ItemWorldMonitorThread, NULL, 0, NULL);
            if (g_monitor_thread == NULL) {
                g_mod_enabled = false;
                return FALSE;
            }
        }
        return TRUE;
    }

    __declspec(dllexport) BOOL WINAPI Mod_Disable() {
        g_mod_enabled = false;
        g_cached_item_world = 0;
        return TRUE;
    }

    __declspec(dllexport) BOOL WINAPI Mod_SetOption(const char* key, const DmModValue* value) {
        if (key == NULL || value == NULL || value->struct_size != sizeof(DmModValue)) return FALSE;
        if (strcmp(key, "levels_per_floor") == 0) {
            if (value->type != DmOptionType::SliderInt) return FALSE;
            g_levels_per_floor = value->int_value;
        } else if (strcmp(key, "auto_subdue") == 0) {
            if (value->type != DmOptionType::Toggle) return FALSE;
            g_auto_subdue = value->bool_value != FALSE;
        } else if (strcmp(key, "mystery_room_rate") == 0) {
            if (value->type != DmOptionType::SliderInt) return FALSE;
            g_mystery_room_rate = value->int_value;
        } else {
            return FALSE;
        }
        return TRUE;
    }

    __declspec(dllexport) void WINAPI Mod_Shutdown() {
        g_mod_enabled = false;
        g_thread_running = false;
        if (g_monitor_thread != NULL) {
            WaitForSingleObject(g_monitor_thread, 500);
            CloseHandle(g_monitor_thread);
            g_monitor_thread = NULL;
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
