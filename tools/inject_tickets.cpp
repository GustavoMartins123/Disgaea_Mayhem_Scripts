#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int main() {
    SetConsoleOutputCP(CP_UTF8);
    printf("============================================================\n");
    printf("  DISGAEA MAYHEM - SYNC BOOST TICKETS (C++ NATIVE)\n");
    printf("============================================================\n");

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        printf("[ERRO] Nao foi possivel acessar a lista de processos.\n");
        return 1;
    }

    PROCESSENTRY32 pe = { sizeof(pe) };
    DWORD pid = 0;
    if (Process32First(hSnap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "Disgaea_Mayhem.exe") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(hSnap, &pe));
    }
    CloseHandle(hSnap);

    if (!pid) {
        printf("[ERRO] O processo Disgaea_Mayhem.exe nao foi encontrado!\n");
        return 1;
    }

    printf("[OK] Jogo encontrado (PID: %lu)\n", pid);
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        printf("[ERRO] Falha ao abrir o processo do jogo.\n");
        return 1;
    }

    MEMORY_BASIC_INFORMATION mbi = {};
    uintptr_t address = 0;
    const uint32_t id_3003 = 3003;
    const uint32_t id_3004 = 3004;
    const uint32_t id_3005 = 3005;
    int matches = 0;

    while (VirtualQueryEx(hProcess, (LPCVOID)address, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
            uint8_t* buffer = (uint8_t*)malloc(mbi.RegionSize);
            SIZE_T br = 0;
            if (buffer && ReadProcessMemory(hProcess, (LPCVOID)address, buffer, mbi.RegionSize, &br)) {
                if (br >= 128) {
                    for (size_t i = 0; i <= br - 128; i += 4) {
                        if (*(uint32_t*)(buffer + i) == id_3003) {
                            bool f3004 = false, f3005 = false;
                            for (size_t j = i + 4; j < i + 128; j += 4) {
                                if (*(uint32_t*)(buffer + j) == id_3004) f3004 = true;
                                if (*(uint32_t*)(buffer + j) == id_3005) f3005 = true;
                            }
                            if (f3004 && f3005) {
                                printf("-> Localizada tabela de Boost Tickets em 0x%016llX\n", (unsigned long long)(address + i));
                                matches++;
                            }
                        }
                    }
                }
                free(buffer);
            }
        }
        address += mbi.RegionSize;
    }

    CloseHandle(hProcess);
    printf("\n[SUCESSO] Operacao concluida! %d tabelas mapeadas.\n", matches);
    return 0;
}
