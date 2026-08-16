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

struct PatchEntry {
    uint32_t rva;
    uint8_t patch[6];
    uint8_t orig[6];
    size_t size;
};

// -----------------------------------------------------------------------------
// Exact Native Code Patches (Replacing Energy Reads with Constant 100)
// -----------------------------------------------------------------------------
static PatchEntry g_patches[] = {
    // 1. RVA 0x453075: mov ecx, [rbx + 0x178] (8B 8B 78 01 00 00) -> mov ecx, 100; nop (B9 64 00 00 00 90)
    { 0x00453075, { 0xB9, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x8B, 0x78, 0x01, 0x00, 0x00 }, 6 },

    // 2. RVA 0x46E1F4: mov edx, [rdx + 0x178] (8B 92 78 01 00 00) -> mov edx, 100; nop (BA 64 00 00 00 90)
    { 0x0046E1F4, { 0xBA, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x92, 0x78, 0x01, 0x00, 0x00 }, 6 },

    // 3. RVA 0x4A342E: mov edx, [rdi + 0x178] (8B 97 78 01 00 00) -> mov edx, 100; nop (BA 64 00 00 00 90)
    { 0x004A342E, { 0xBA, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x97, 0x78, 0x01, 0x00, 0x00 }, 6 },

    // 4. RVA 0x4944B3: mov eax, [rcx + 0x178] (8B 81 78 01 00 00) -> mov eax, 100; nop (B8 64 00 00 00 90)
    { 0x004944B3, { 0xB8, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x81, 0x78, 0x01, 0x00, 0x00 }, 6 },

    // 5. RVA 0x4979A6: mov eax, [rcx + 0x178] (8B 81 78 01 00 00) -> mov eax, 100; nop (B8 64 00 00 00 90)
    { 0x004979A6, { 0xB8, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x81, 0x78, 0x01, 0x00, 0x00 }, 6 },

    // 6. RVA 0x4B1D12: mov edi, [rdi + 0x178] (8B BF 78 01 00 00) -> mov edi, 100; nop (BF 64 00 00 00 90)
    { 0x004B1D12, { 0xBF, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0xBF, 0x78, 0x01, 0x00, 0x00 }, 6 },

    // 7. RVA 0x4B1DA4: mov ecx, [rdi + 0x178] (8B 8F 78 01 00 00) -> mov ecx, 100; nop (B9 64 00 00 00 90)
    { 0x004B1DA4, { 0xB9, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x8F, 0x78, 0x01, 0x00, 0x00 }, 6 },

    // 8. RVA 0x4B8564: mov edx, [rax + 0x178] (8B 90 78 01 00 00) -> mov edx, 100; nop (BA 64 00 00 00 90)
    { 0x004B8564, { 0xBA, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x90, 0x78, 0x01, 0x00, 0x00 }, 6 }
};

static const size_t g_num_patches = sizeof(g_patches) / sizeof(g_patches[0]);

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

    for (size_t i = 0; i < g_num_patches; ++i) {
        uintptr_t addr = g_exe_base + g_patches[i].rva;
        PatchCodeBytes(addr, g_patches[i].patch, g_patches[i].size, g_patches[i].orig);
    }
}

static void RestoreNativeCodePatches() {
    if (!g_exe_base) return;

    for (size_t i = 0; i < g_num_patches; ++i) {
        uintptr_t addr = g_exe_base + g_patches[i].rva;
        PatchCodeBytes(addr, g_patches[i].orig, g_patches[i].size);
    }
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
