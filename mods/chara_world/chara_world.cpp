#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <algorithm>

// -----------------------------------------------------------------------------
// Global Chara World Mod State
// -----------------------------------------------------------------------------
static bool g_mod_enabled = true;
static int g_target_energy = 100;
static bool g_freeze_energy = true;
static HANDLE g_monitor_thread = NULL;
static bool g_thread_running = false;

// Dynamic VTable addresses (calculated with ASLR Base)
static uintptr_t g_exe_base = 0;
static uintptr_t g_vtable_cw_info = 0;
static uintptr_t g_vtable_cw_energy_ui = 0;

// -----------------------------------------------------------------------------
// Resident Memory Hook & Continuous Energy Monitor Thread
// -----------------------------------------------------------------------------
static DWORD WINAPI CharaWorldMonitorThread(LPVOID lpParam) {
    g_thread_running = true;

    g_exe_base = (uintptr_t)GetModuleHandleA(NULL);
    if (!g_exe_base) g_exe_base = 0x140000000;

    g_vtable_cw_info = g_exe_base + 0xA57610;
    g_vtable_cw_energy_ui = g_exe_base + 0xA71728;

    SYSTEM_INFO sys_info = {};
    GetSystemInfo(&sys_info);
    const uintptr_t min_addr = (uintptr_t)sys_info.lpMinimumApplicationAddress;
    const uintptr_t max_addr = (uintptr_t)sys_info.lpMaximumApplicationAddress;

    while (g_thread_running) {
        if (g_mod_enabled && g_freeze_energy) {
            uintptr_t address = min_addr;
            MEMORY_BASIC_INFORMATION mbi = {};

            while (address < max_addr && VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi))) {
                if (mbi.State == MEM_COMMIT &&
                    (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
                    uint8_t* buffer = (uint8_t*)mbi.BaseAddress;
                    const size_t size = mbi.RegionSize;

                    if (size >= 0x200) {
                        for (size_t i = 0; i <= size - 0x200; i += 8) {
                            uintptr_t vptr = *(uintptr_t*)(buffer + i);
                            
                            // 1. Monitor CCharacterWorldInformation (Master Logic Energy)
                            if (vptr == g_vtable_cw_info) {
                                int32_t* p_energy = (int32_t*)(buffer + i + 0x178);
                                if (*p_energy != g_target_energy && *p_energy >= 0 && *p_energy <= 9999) {
                                    *p_energy = g_target_energy;
                                }
                            }
                            // 2. Monitor CUIUnion_CharacterWorld_Energy (Visual UI Energy Display)
                            else if (vptr == g_vtable_cw_energy_ui) {
                                int32_t* p_cur = (int32_t*)(buffer + i + 0x70);
                                int32_t* p_bar = (int32_t*)(buffer + i + 0x78);
                                int32_t* p_tgt = (int32_t*)(buffer + i + 0x7C);
                                int32_t* p_dsp = (int32_t*)(buffer + i + 0x80);

                                if (*p_cur != g_target_energy && *p_cur >= 0 && *p_cur <= 9999) {
                                    *p_cur = g_target_energy;
                                    *p_bar = g_target_energy;
                                    *p_tgt = g_target_energy;
                                    *p_dsp = g_target_energy;
                                }
                            }
                        }
                    }
                }
                address = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            }
        }
        Sleep(80); // Fast 80ms scan interval for seamless instant energy lock
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
