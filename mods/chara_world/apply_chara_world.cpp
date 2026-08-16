#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

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
        printf("[ERRO] Disgaea_Mayhem.exe não está em execução!\n");
        printf("Inicie o jogo antes de executar este utilitário.\n");
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
        printf("[ERRO] Não foi possível obter o Base Address de Disgaea_Mayhem.exe.\n");
        CloseHandle(hProc);
        system("pause");
        return 1;
    }

    printf("[OK] Processo detectado (PID %lu) | Base Address: 0x%016llX\n", pid, (unsigned long long)exe_base);

    const uint8_t nop6[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

    // 1. Patch DEC dword ptr [rbx + 0x178] at RVA 0x003C4ABF
    uintptr_t target_dec = exe_base + 0x3C4ABF;
    bool p1 = PatchProcessCode(hProc, target_dec, nop6, 6);
    printf("[1/3] Patch DEC Energia (RVA 0x3C4ABF): %s\n", p1 ? "APLICADO COM SUCESSO" : "FALHA");

    // 2. Patch MOV [rsi + 0x178], eax at RVA 0x004AD281
    uintptr_t target_mov1 = exe_base + 0x4AD281;
    bool p2 = PatchProcessCode(hProc, target_mov1, nop6, 6);
    printf("[2/3] Patch Escrita Turno (RVA 0x4AD281): %s\n", p2 ? "APLICADO COM SUCESSO" : "FALHA");

    // 3. Patch MOV [rbp + 0x178], eax at RVA 0x004BA6EE
    uintptr_t target_mov2 = exe_base + 0x4BA6EE;
    bool p3 = PatchProcessCode(hProc, target_mov2, nop6, 6);
    printf("[3/3] Patch Fim de Turno (RVA 0x4BA6EE): %s\n", p3 ? "APLICADO COM SUCESSO" : "FALHA");

    if (p1 && p2 && p3) {
        printf("\n>>> SUCESSO: Instruções nativas de decremento de energia desativadas no processador!\n");
        printf("A energia do Chara World agora nunca diminui durante os passos.\n");
    } else {
        printf("\n[AVISO] Um ou mais patches de instrução falharam.\n");
    }

    CloseHandle(hProc);
    Sleep(2000);
    return 0;
}
