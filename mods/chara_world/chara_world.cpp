#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

static HMODULE g_module = nullptr;
static uintptr_t g_exe_base = 0;
static volatile LONG g_enabled = 0;
static volatile LONG g_patches_applied = 0;

struct PatchEntry {
    uint32_t rva;
    uint8_t patch[6];
    uint8_t expected[6];
    bool owned;
    const char* desc;
};

static PatchEntry g_patches[] = {
    { 0x00453075, { 0xB9, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x8B, 0x78, 0x01, 0x00, 0x00 }, false, "Calculo de Passos" },
    { 0x0046E1F4, { 0xBA, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x92, 0x78, 0x01, 0x00, 0x00 }, false, "Logica de Turno" },
    { 0x004A342E, { 0xBA, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x97, 0x78, 0x01, 0x00, 0x00 }, false, "Verificacao de Acoes" },
    { 0x004944B3, { 0xB8, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x81, 0x78, 0x01, 0x00, 0x00 }, false, "Check Energia Restante 1" },
    { 0x004979A6, { 0xB8, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x81, 0x78, 0x01, 0x00, 0x00 }, false, "Check Energia Restante 2" },
    { 0x004B1D12, { 0xBF, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0xBF, 0x78, 0x01, 0x00, 0x00 }, false, "Display Visual HUD" },
    { 0x004B1DA4, { 0xB9, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x8F, 0x78, 0x01, 0x00, 0x00 }, false, "Renderizador de HUD" },
    { 0x004B8564, { 0xBA, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x90, 0x78, 0x01, 0x00, 0x00 }, false, "Acao de Tabuleiro" }
};

static const size_t g_num_patches = sizeof(g_patches) / sizeof(g_patches[0]);

static void Log(const char* fmt, ...) {
    char dll_path[MAX_PATH] = {};
    if (!g_module || !GetModuleFileNameA(g_module, dll_path, MAX_PATH)) return;
    char* slash = strrchr(dll_path, '\\');
    if (!slash) return;
    *slash = '\0';

    char log_path[MAX_PATH] = {};
    snprintf(log_path, sizeof(log_path), "%s\\chara_world.log", dll_path);
    FILE* f = fopen(log_path, "a");
    if (!f) return;

    SYSTEMTIME now = {};
    GetLocalTime(&now);
    fprintf(f, "[%02u:%02u:%02u.%03u][T%lu] ", now.wHour, now.wMinute, now.wSecond,
            now.wMilliseconds, GetCurrentThreadId());
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}

static bool IsReadableCode(uintptr_t address, size_t size) {
    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    return address >= (uintptr_t)mbi.BaseAddress && size <= end - address;
}

static bool BytesEqual(uintptr_t address, const uint8_t* bytes, size_t size) {
    return IsReadableCode(address, size) && memcmp((const void*)address, bytes, size) == 0;
}

static bool WriteCode(uintptr_t address, const uint8_t* bytes, size_t size) {
    DWORD old_protect = 0;
    if (!VirtualProtect((LPVOID)address, size, PAGE_EXECUTE_READWRITE, &old_protect)) return false;
    memcpy((void*)address, bytes, size);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)address, size);
    DWORD ignored = 0;
    VirtualProtect((LPVOID)address, size, old_protect, &ignored);
    return true;
}

static bool PreflightPatches() {
    if (!g_exe_base) g_exe_base = (uintptr_t)GetModuleHandleA(nullptr);
    if (!g_exe_base) {
        Log("[ERROR] Nao foi possivel obter o base address do executavel.");
        return false;
    }

    for (size_t i = 0; i < g_num_patches; ++i) {
        const uintptr_t address = g_exe_base + g_patches[i].rva;
        const bool original = BytesEqual(address, g_patches[i].expected, 6);
        const bool already_patched = BytesEqual(address, g_patches[i].patch, 6);
        if (!original && !already_patched) {
            uint8_t current[6] = {};
            if (IsReadableCode(address, sizeof(current))) memcpy(current, (const void*)address, sizeof(current));
            Log("[PREFLIGHT_ERROR] %s RVA=0x%08X bytes=%02X %02X %02X %02X %02X %02X",
                g_patches[i].desc, g_patches[i].rva,
                current[0], current[1], current[2], current[3], current[4], current[5]);
            return false;
        }
    }
    return true;
}

static bool ApplyNativeCodePatches() {
    if (InterlockedCompareExchange(&g_patches_applied, 0, 0) != 0) return true;
    if (!PreflightPatches()) {
        Log("[DISABLED] Nenhum patch aplicado porque a build do jogo nao corresponde aos bytes esperados.");
        return false;
    }

    size_t changed = 0;
    for (size_t i = 0; i < g_num_patches; ++i) {
        const uintptr_t address = g_exe_base + g_patches[i].rva;
        g_patches[i].owned = false;
        if (BytesEqual(address, g_patches[i].patch, 6)) {
            Log("[PATCH] %s ja estava aplicado; mantendo sem assumir ownership.", g_patches[i].desc);
            continue;
        }
        if (!WriteCode(address, g_patches[i].patch, 6)) {
            Log("[PATCH_ERROR] Falha ao aplicar %s RVA=0x%08X; revertendo patches desta DLL.",
                g_patches[i].desc, g_patches[i].rva);
            for (size_t r = 0; r < i; ++r) {
                if (g_patches[r].owned) {
                    const uintptr_t restore_address = g_exe_base + g_patches[r].rva;
                    if (BytesEqual(restore_address, g_patches[r].patch, 6)) {
                        WriteCode(restore_address, g_patches[r].expected, 6);
                    }
                    g_patches[r].owned = false;
                }
            }
            return false;
        }
        g_patches[i].owned = true;
        ++changed;
        Log("[PATCH] %s aplicado RVA=0x%08X", g_patches[i].desc, g_patches[i].rva);
    }

    InterlockedExchange(&g_patches_applied, 1);
    Log("[ENABLE] Chara World ativo (%zu patches escritos por esta DLL).", changed);
    return true;
}

static void RestoreNativeCodePatches() {
    if (InterlockedCompareExchange(&g_patches_applied, 0, 0) == 0) return;

    for (size_t i = 0; i < g_num_patches; ++i) {
        if (!g_patches[i].owned) continue;
        const uintptr_t address = g_exe_base + g_patches[i].rva;
        if (BytesEqual(address, g_patches[i].patch, 6)) {
            if (WriteCode(address, g_patches[i].expected, 6)) {
                Log("[RESTORE] %s restaurado RVA=0x%08X", g_patches[i].desc, g_patches[i].rva);
            } else {
                Log("[RESTORE_ERROR] Falha ao restaurar %s RVA=0x%08X", g_patches[i].desc, g_patches[i].rva);
            }
        } else {
            Log("[RESTORE_SKIP] %s mudou externamente; nao sobrescrevendo.", g_patches[i].desc);
        }
        g_patches[i].owned = false;
    }

    InterlockedExchange(&g_patches_applied, 0);
}

extern "C" {
    __declspec(dllexport) void Mod_Enable() {
        if (InterlockedCompareExchange(&g_enabled, 1, 1) != 0) return;
        if (ApplyNativeCodePatches()) {
            InterlockedExchange(&g_enabled, 1);
        } else {
            InterlockedExchange(&g_enabled, 0);
        }
    }

    __declspec(dllexport) void Mod_Disable() {
        if (InterlockedExchange(&g_enabled, 0) == 0 &&
            InterlockedCompareExchange(&g_patches_applied, 0, 0) == 0) return;
        RestoreNativeCodePatches();
        Log("[DISABLE] Chara World desativado.");
    }

    __declspec(dllexport) void* start_mod() {
        Mod_Enable();
        return InterlockedCompareExchange(&g_enabled, 0, 0) ? (void*)1 : nullptr;
    }

    __declspec(dllexport) void uninstall_mod(void*) {
        Mod_Disable();
    }

    __declspec(dllexport) void Mod_SetOption(const char*, int, bool) {}

    __declspec(dllexport) bool Mod_IsActive() {
        return InterlockedCompareExchange(&g_enabled, 0, 0) != 0 &&
               InterlockedCompareExchange(&g_patches_applied, 0, 0) != 0;
    }
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        g_exe_base = (uintptr_t)GetModuleHandleA(nullptr);
        // DllMain intentionally stays passive. The host calls Mod_Enable outside loader lock.
    }
    return TRUE;
}
