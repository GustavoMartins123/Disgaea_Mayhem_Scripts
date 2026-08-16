#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

struct PatchEntry {
    uint32_t rva;
    uint8_t patch[6];
    const char* desc;
};

static PatchEntry g_patches[] = {
    { 0x00453075, { 0xB9, 0x64, 0x00, 0x00, 0x00, 0x90 }, "Calculo de Passos (mov ecx, 100)" },
    { 0x0046E1F4, { 0xBA, 0x64, 0x00, 0x00, 0x00, 0x90 }, "Logica de Turno (mov edx, 100)" },
    { 0x004A342E, { 0xBA, 0x64, 0x00, 0x00, 0x00, 0x90 }, "Verificacao de Acoes (mov edx, 100)" },
    { 0x004944B3, { 0xB8, 0x64, 0x00, 0x00, 0x00, 0x90 }, "Check Energia Restante 1 (mov eax, 100)" },
    { 0x004979A6, { 0xB8, 0x64, 0x00, 0x00, 0x00, 0x90 }, "Check Energia Restante 2 (mov eax, 100)" },
    { 0x004B1D12, { 0xBF, 0x64, 0x00, 0x00, 0x00, 0x90 }, "Display Visual HUD (mov edi, 100)" },
    { 0x004B1DA4, { 0xB9, 0x64, 0x00, 0x00, 0x00, 0x90 }, "Renderizador de HUD (mov ecx, 100)" },
    { 0x004B8564, { 0xBA, 0x64, 0x00, 0x00, 0x00, 0x90 }, "Acao de Tabuleiro (mov edx, 100)" }
};

static const size_t g_num_patches = sizeof(g_patches) / sizeof(g_patches[0]);

DWORD GetGamePID() {
    DWORD pid = 0;
    PROCESSENTRY32W pe = { sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"Disgaea_Mayhem.exe") == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    return pid;
}

uintptr_t GetProcessBaseAddress(HANDLE hProc) {
    HMODULE hMods[1024] = {};
    DWORD cbNeeded = 0;
    if (EnumProcessModules(hProc, hMods, sizeof(hMods), &cbNeeded)) {
        return (uintptr_t)hMods[0];
    }
    return 0;
}

bool PatchProcessCode(HANDLE hProc, uintptr_t addr, const uint8_t* patch, size_t size) {
    DWORD old_protect = 0;
    if (!VirtualProtectEx(hProc, (LPVOID)addr, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }
    SIZE_T written = 0;
    bool res = WriteProcessMemory(hProc, (LPVOID)addr, patch, size, &written) && (written == size);
    VirtualProtectEx(hProc, (LPVOID)addr, size, old_protect, &old_protect);
    FlushInstructionCache(hProc, (LPCVOID)addr, size);
    return res;
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    printf("=================================================================\n");
    printf("  Disgaea Mayhem - Chara World Patch Nativo de Assembly (CPU)\n");
    printf("=================================================================\n\n");

    DWORD pid = GetGamePID();
    if (!pid) {
        printf("[ERRO] Disgaea_Mayhem.exe nao esta em execucao!\n");
        printf("Inicie o jogo antes de executar este utilitario.\n");
        system("pause");
        return 1;
    }

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) {
        printf("[ERRO] Falha ao abrir o processo (PID %lu). Execute como Administrador.\n", pid);
        system("pause");
        return 1;
    }

    uintptr_t exe_base = GetProcessBaseAddress(hProc);
    if (!exe_base) {
        printf("[ERRO] Nao foi possivel obter o Base Address de Disgaea_Mayhem.exe.\n");
        CloseHandle(hProc);
        system("pause");
        return 1;
    }

    printf("[OK] Processo detectado (PID %lu) | Base Address: 0x%016llX\n\n", pid, (unsigned long long)exe_base);

    int applied = 0;
    for (size_t i = 0; i < g_num_patches; ++i) {
        uintptr_t target_addr = exe_base + g_patches[i].rva;
        bool ok = PatchProcessCode(hProc, target_addr, g_patches[i].patch, 6);
        printf("[%zu/%zu] %s (RVA 0x%06X): %s\n",
               i + 1, g_num_patches, g_patches[i].desc, g_patches[i].rva,
               ok ? "APLICADO COM SUCESSO" : "FALHA");
        if (ok) applied++;
    }

    if (applied == g_num_patches) {
        printf("\n>>> SUCESSO: Todas as %d rotinas nativas de leitura de energia cravadas em 100!\n", applied);
        printf("A energia do Chara World agora se mantem em 100/100 permanentemente.\n");
    } else {
        printf("\n[AVISO] %d de %zu patches foram aplicados.\n", applied, g_num_patches);
    }

    CloseHandle(hProc);
    Sleep(2000);
    return 0;
}
