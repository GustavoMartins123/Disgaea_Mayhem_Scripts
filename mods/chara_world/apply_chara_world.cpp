#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

struct PatchEntry {
    uint32_t rva;
    uint8_t patch[6];
    uint8_t expected[6];
    const char* desc;
};

static PatchEntry g_patches[] = {
    { 0x00453075, { 0xB9, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x8B, 0x78, 0x01, 0x00, 0x00 }, "Calculo de Passos" },
    { 0x0046E1F4, { 0xBA, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x92, 0x78, 0x01, 0x00, 0x00 }, "Logica de Turno" },
    { 0x004A342E, { 0xBA, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x97, 0x78, 0x01, 0x00, 0x00 }, "Verificacao de Acoes" },
    { 0x004944B3, { 0xB8, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x81, 0x78, 0x01, 0x00, 0x00 }, "Check Energia Restante 1" },
    { 0x004979A6, { 0xB8, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x81, 0x78, 0x01, 0x00, 0x00 }, "Check Energia Restante 2" },
    { 0x004B1D12, { 0xBF, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0xBF, 0x78, 0x01, 0x00, 0x00 }, "Display Visual HUD" },
    { 0x004B1DA4, { 0xB9, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x8F, 0x78, 0x01, 0x00, 0x00 }, "Renderizador de HUD" },
    { 0x004B8564, { 0xBA, 0x64, 0x00, 0x00, 0x00, 0x90 }, { 0x8B, 0x90, 0x78, 0x01, 0x00, 0x00 }, "Acao de Tabuleiro" }
};

static const size_t g_num_patches = sizeof(g_patches) / sizeof(g_patches[0]);

static DWORD GetGamePID() {
    PROCESSENTRY32W pe = { sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"Disgaea_Mayhem.exe") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static uintptr_t GetProcessBaseAddress(HANDLE process) {
    HMODULE modules[1024] = {};
    DWORD needed = 0;
    if (!EnumProcessModules(process, modules, sizeof(modules), &needed) || needed < sizeof(HMODULE)) return 0;
    return (uintptr_t)modules[0];
}

static bool ReadBytes(HANDLE process, uintptr_t address, uint8_t* out, size_t size) {
    SIZE_T read = 0;
    return ReadProcessMemory(process, (LPCVOID)address, out, size, &read) && read == size;
}

static bool BytesEqual(HANDLE process, uintptr_t address, const uint8_t* expected, size_t size) {
    uint8_t current[16] = {};
    if (size > sizeof(current) || !ReadBytes(process, address, current, size)) return false;
    return memcmp(current, expected, size) == 0;
}

static bool WriteCode(HANDLE process, uintptr_t address, const uint8_t* bytes, size_t size) {
    DWORD old_protect = 0;
    if (!VirtualProtectEx(process, (LPVOID)address, size, PAGE_EXECUTE_READWRITE, &old_protect)) return false;
    SIZE_T written = 0;
    const bool ok = WriteProcessMemory(process, (LPVOID)address, bytes, size, &written) && written == size;
    FlushInstructionCache(process, (LPCVOID)address, size);
    DWORD ignored = 0;
    VirtualProtectEx(process, (LPVOID)address, size, old_protect, &ignored);
    return ok;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    printf("=================================================================\n");
    printf("  Disgaea Mayhem - Chara World Patch Validado\n");
    printf("=================================================================\n\n");

    const DWORD pid = GetGamePID();
    if (!pid) {
        printf("[ERRO] Disgaea_Mayhem.exe nao esta em execucao.\n");
        return 1;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION,
                                 FALSE, pid);
    if (!process) {
        printf("[ERRO] Nao foi possivel abrir o processo (Win32=%lu).\n", GetLastError());
        return 2;
    }

    const uintptr_t base = GetProcessBaseAddress(process);
    if (!base) {
        printf("[ERRO] Nao foi possivel obter o base address.\n");
        CloseHandle(process);
        return 3;
    }

    // Preflight completo: nada e escrito se algum RVA nao corresponder a esta build.
    for (size_t i = 0; i < g_num_patches; ++i) {
        const uintptr_t address = base + g_patches[i].rva;
        if (!BytesEqual(process, address, g_patches[i].expected, 6) &&
            !BytesEqual(process, address, g_patches[i].patch, 6)) {
            uint8_t current[6] = {};
            ReadBytes(process, address, current, sizeof(current));
            printf("[BLOQUEADO] %s RVA 0x%08X nao corresponde a build esperada.\n", g_patches[i].desc, g_patches[i].rva);
            printf("            Atual: %02X %02X %02X %02X %02X %02X\n",
                   current[0], current[1], current[2], current[3], current[4], current[5]);
            printf("Nenhum patch foi aplicado.\n");
            CloseHandle(process);
            return 4;
        }
    }

    size_t applied = 0;
    for (size_t i = 0; i < g_num_patches; ++i) {
        const uintptr_t address = base + g_patches[i].rva;
        if (BytesEqual(process, address, g_patches[i].patch, 6)) {
            printf("[%zu/%zu] %s: JA APLICADO\n", i + 1, g_num_patches, g_patches[i].desc);
            continue;
        }
        if (!WriteCode(process, address, g_patches[i].patch, 6)) {
            printf("[%zu/%zu] %s: FALHA (Win32=%lu)\n", i + 1, g_num_patches, g_patches[i].desc, GetLastError());
            CloseHandle(process);
            return 5;
        }
        ++applied;
        printf("[%zu/%zu] %s: APLICADO\n", i + 1, g_num_patches, g_patches[i].desc);
    }

    printf("\n[OK] Validacao concluida; %zu patch(es) novos aplicados.\n", applied);
    CloseHandle(process);
    return 0;
}
