#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// -----------------------------------------------------------------------------
// Global Mod State
// -----------------------------------------------------------------------------
static volatile bool g_mod_enabled = false;
static uintptr_t g_exe_base = 0;

// Original opcodes for clean restoration on Mod_Disable()
static uint8_t g_orig_dec[6] = { 0xFF, 0x8B, 0x78, 0x01, 0x00, 0x00 };
static uint8_t g_orig_mov1[6] = { 0x89, 0x86, 0x78, 0x01, 0x00, 0x00 };
static uint8_t g_orig_mov2[6] = { 0x89, 0x85, 0x78, 0x01, 0x00, 0x00 };

static const uint8_t g_nop6[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

// -----------------------------------------------------------------------------
// Direct Memory Byte Patch Helper
// -----------------------------------------------------------------------------
static bool PatchCodeBytes(uintptr_t address, const uint8_t* patch, size_t size, uint8_t* out_backup = nullptr) {
    if (!address) return false;

    DWORD old_protect = 0;
    if (!VirtualProtect((LPVOID)address, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }

    if (out_backup) {
        memcpy(out_backup, (const void*)address, size);
    }

    memcpy((void*)address, patch, size);
    VirtualProtect((LPVOID)address, size, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)address, size);
    return true;
}

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
// Apply Native Code Patches (Assembly Level)
// -----------------------------------------------------------------------------
static void ApplyNativeCodePatches() {
    if (!g_exe_base) {
        g_exe_base = (uintptr_t)GetModuleHandleA(NULL);
        if (!g_exe_base) g_exe_base = 0x140000000;
    }

    // 1. Patch DEC dword ptr [rbx + 0x178] at RVA 0x003C4ABF -> NOP (Disables energy decrement on step)
    uintptr_t rva_dec = g_exe_base + 0x3C4ABF;
    PatchCodeBytes(rva_dec, g_nop6, 6, g_orig_dec);

    // 2. Patch MOV [rsi + 0x178], eax at RVA 0x004AD281 -> NOP (Prevents overriding energy with depleted value)
    uintptr_t rva_mov1 = g_exe_base + 0x4AD281;
    PatchCodeBytes(rva_mov1, g_nop6, 6, g_orig_mov1);

    // 3. Patch MOV [rbp + 0x178], eax at RVA 0x004BA6EE -> NOP (Prevents turn end energy decrement)
    uintptr_t rva_mov2 = g_exe_base + 0x4BA6EE;
    PatchCodeBytes(rva_mov2, g_nop6, 6, g_orig_mov2);
}

static void RestoreNativeCodePatches() {
    if (!g_exe_base) return;

    // Restore original CPU opcodes
    PatchCodeBytes(g_exe_base + 0x3C4ABF, g_orig_dec, 6);
    PatchCodeBytes(g_exe_base + 0x4AD281, g_orig_mov1, 6);
    PatchCodeBytes(g_exe_base + 0x4BA6EE, g_orig_mov2, 6);
}

// -----------------------------------------------------------------------------
// Mod Plugin Interface Exports (UE4SS + ModMenu Standard)
// -----------------------------------------------------------------------------
extern "C" {
    __declspec(dllexport) void Mod_Enable() {
        g_mod_enabled = true;
        ApplyNativeCodePatches();
    }

    __declspec(dllexport) void Mod_Disable() {
        g_mod_enabled = false;
        RestoreNativeCodePatches();
    }

    __declspec(dllexport) void* start_mod() {
        Mod_Enable();
        return (void*)1;
    }

    __declspec(dllexport) void uninstall_mod(void*) {
        Mod_Disable();
    }

    __declspec(dllexport) void Mod_SetOption(const char* key, int int_val, bool bool_val) {
        (void)key; (void)int_val; (void)bool_val;
    }

    __declspec(dllexport) bool Mod_IsActive() {
        return g_mod_enabled;
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

            if (CheckEnabledTxt()) {
                Mod_Enable();
            } else {
                Mod_Disable();
            }
            break;
        }
        case DLL_PROCESS_DETACH:
            g_mod_enabled = false;
            RestoreNativeCodePatches();
            break;
    }
    return TRUE;
}
