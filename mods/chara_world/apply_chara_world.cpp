#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <vector>

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

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    printf("=================================================================\n");
    printf("  Disgaea Mayhem - Chara World Energia Infinita (Travar em 100)\n");
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
        printf("[ERRO] Nao foi possivel abrir o processo (PID %lu). Execute como Administrador.\n", pid);
        system("pause");
        return 1;
    }

    printf("[OK] Processo encontrado (PID %lu). Varrendo memoria...\n", pid);

    SYSTEM_INFO si = {};
    GetSystemInfo(&si);

    uintptr_t curr = 0x10000;
    uintptr_t max_addr = (uintptr_t)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi = {};

    std::vector<uintptr_t> energy_addrs;

    while (curr < max_addr && VirtualQueryEx(hProc, (LPCVOID)curr, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_READWRITE) && !(mbi.Protect & PAGE_GUARD)) {
            std::vector<uint8_t> buffer(mbi.RegionSize);
            SIZE_T bytesRead = 0;
            if (ReadProcessMemory(hProc, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &bytesRead) && bytesRead >= 16) {
                for (size_t i = 0; i + 16 <= bytesRead; i += 4) {
                    int32_t* p = (int32_t*)(buffer.data() + i);
                    if (p[1] == 100 && p[0] >= 0 && p[0] <= 100 && p[2] == 0) {
                        uintptr_t addr = (uintptr_t)mbi.BaseAddress + i;
                        int32_t val = 100;
                        if (WriteProcessMemory(hProc, (LPVOID)addr, &val, 4, NULL)) {
                            energy_addrs.push_back(addr);
                        }
                    }
                }
            }
        }
        curr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }

    printf("[SUCESSO] %zu blocos de energia do Chara World identificados e travados em 100!\n", energy_addrs.size());

    // Se iniciado manualmente, entra em modo guardiao continuo
    printf("\n[MODO GUARDIÃO ATIVO] Mantendo energia em 100 em tempo real...\n");
    printf("Pressione Ctrl+C para fechar quando terminar o Chara World.\n\n");

    int ticks = 0;
    while (true) {
        ticks++;
        for (uintptr_t addr : energy_addrs) {
            int32_t val = 100;
            WriteProcessMemory(hProc, (LPVOID)addr, &val, 4, NULL);
        }

        if (ticks % 20 == 0) {
            // Re-sweep periodically in case of new turn allocations
            curr = 0x10000;
            while (curr < max_addr && VirtualQueryEx(hProc, (LPCVOID)curr, &mbi, sizeof(mbi))) {
                if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_READWRITE) && !(mbi.Protect & PAGE_GUARD)) {
                    std::vector<uint8_t> buffer(mbi.RegionSize);
                    SIZE_T bytesRead = 0;
                    if (ReadProcessMemory(hProc, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &bytesRead) && bytesRead >= 16) {
                        for (size_t i = 0; i + 16 <= bytesRead; i += 4) {
                            int32_t* p = (int32_t*)(buffer.data() + i);
                            if (p[1] == 100 && p[0] >= 0 && p[0] <= 100 && p[2] == 0) {
                                uintptr_t addr = (uintptr_t)mbi.BaseAddress + i;
                                int32_t val = 100;
                                WriteProcessMemory(hProc, (LPVOID)addr, &val, 4, NULL);
                            }
                        }
                    }
                }
                curr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            }
        }
        Sleep(50);
    }

    CloseHandle(hProc);
    return 0;
}
