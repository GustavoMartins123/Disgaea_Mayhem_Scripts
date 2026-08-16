#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <vector>

// -----------------------------------------------------------------------------
// Global Chara World Mod State
// -----------------------------------------------------------------------------
static volatile bool g_mod_enabled = false;
static volatile int g_target_energy = 100;
static volatile bool g_freeze_energy = true;
static HANDLE g_monitor_thread = NULL;
static volatile bool g_thread_running = false;

// Dynamic cached pointers to active energy addresses
static std::vector<uintptr_t> g_active_energy_addrs;
static CRITICAL_SECTION g_cs;

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

// -----------------------------------------------------------------------------
// Fast In-Process Energy Scanner & Continuous Freezing Engine
// -----------------------------------------------------------------------------
static void ScanAndFreezeAllEnergyBlocks() {
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);

    uintptr_t curr = 0x10000;
    uintptr_t max_addr = (uintptr_t)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi = {};

    std::vector<uintptr_t> found;

    while (curr < max_addr && VirtualQuery((LPCVOID)curr, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT && 
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE) && 
            !(mbi.Protect & PAGE_GUARD)) {
            
            uint8_t* base = (uint8_t*)mbi.BaseAddress;
            size_t size = mbi.RegionSize;

            for (size_t i = 0; i + 16 <= size; i += 4) {
                int32_t* p = (int32_t*)(base + i);
                // Match Chara World Energy signature:
                // p[0] = current_energy (1..100)
                // p[1] = max_energy (100)
                // p[2] == 0
                if (p[1] == 100 && p[0] >= 0 && p[0] <= 100 && p[2] == 0) {
                    uintptr_t addr = (uintptr_t)p;
                    *p = g_target_energy;
                    found.push_back(addr);
                }
            }
        }
        curr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }

    EnterCriticalSection(&g_cs);
    g_active_energy_addrs = found;
    LeaveCriticalSection(&g_cs);
}

static DWORD WINAPI CharaWorldMonitorThread(LPVOID lpParam) {
    g_thread_running = true;

    int tick = 0;

    while (g_thread_running) {
        if (++tick % 20 == 0) {
            if (!CheckEnabledTxt()) {
                g_mod_enabled = false;
            }
        }

        if (g_mod_enabled && g_freeze_energy) {
            // 1. Fast path: Maintain all cached energy addresses
            EnterCriticalSection(&g_cs);
            bool has_valid_cache = false;
            for (uintptr_t addr : g_active_energy_addrs) {
                if (IsValidMemoryRange(addr, 16, PAGE_READWRITE)) {
                    int32_t* p = (int32_t*)addr;
                    if (p[1] == 100) {
                        if (*p != g_target_energy) {
                            *p = g_target_energy;
                        }
                        has_valid_cache = true;
                    }
                }
            }
            LeaveCriticalSection(&g_cs);

            // 2. Periodic sweep every 500ms or if cache is empty
            if (!has_valid_cache || (tick % 10 == 0)) {
                ScanAndFreezeAllEnergyBlocks();
            }
        }

        Sleep(50);
    }

    return 0;
}

// -----------------------------------------------------------------------------
// Mod Plugin Interface Exports (UE4SS + ModMenu Standard)
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
        EnterCriticalSection(&g_cs);
        g_active_energy_addrs.clear();
        LeaveCriticalSection(&g_cs);
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
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hinstDLL);
            InitializeCriticalSection(&g_cs);

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
            DeleteCriticalSection(&g_cs);
            break;
    }
    return TRUE;
}
