#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    SetConsoleOutputCP(CP_UTF8);

    printf("=================================================================\n");
    printf("  DISGAEA MAYHEM - ITEM WORLD RESIDENT HOOK (C++ NATIVE)\n");
    printf("=================================================================\n");

    // 1. Localizar processo Disgaea_Mayhem.exe
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        printf("[ERRO] Falha ao listar processos.\n");
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
        printf("[INFO] Disgaea_Mayhem.exe nao esta aberto.\n");
        printf("       O hook sera carregado automaticamente quando o jogo iniciar com o Mod Menu.\n");
        printf("=================================================================\n");
        return 0;
    }

    printf("[OK] Jogo detectado (PID: %lu)\n", pid);
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        printf("[ERRO] Falha ao abrir o processo do jogo.\n");
        return 1;
    }

    // Injetar item_world.dll se ainda não estiver carregada
    char dll_full_path[MAX_PATH] = {};
    GetFullPathNameA("item_world.dll", MAX_PATH, dll_full_path, NULL);

    if (GetFileAttributesA(dll_full_path) == INVALID_FILE_ATTRIBUTES) {
        GetFullPathNameA("mods/item_world/item_world.dll", MAX_PATH, dll_full_path, NULL);
    }

    printf("[OK] Injetando modulo nativo: %s\n", dll_full_path);

    size_t len = strlen(dll_full_path) + 1;
    LPVOID pRemoteMem = VirtualAllocEx(hProcess, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (pRemoteMem) {
        WriteProcessMemory(hProcess, pRemoteMem, dll_full_path, len, NULL);
        HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
        LPTHREAD_START_ROUTINE pfnLoadLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "LoadLibraryA");
        
        HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pfnLoadLibrary, pRemoteMem, 0, NULL);
        if (hThread) {
            WaitForSingleObject(hThread, 2000);
            CloseHandle(hThread);
            printf("[SUCESSO] Hook residente do Item World ATIVADO (ON) na memoria do jogo!\n");
            printf("  -> Multiplicador ativo para qualquer andar/onda explorada.\n");
            printf("  -> Inocentes sao 100%% subjugados e duplicados automaticamente.\n");
        } else {
            printf("[AVISO] Nao foi possivel criar thread remota.\n");
        }
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
    }

    CloseHandle(hProcess);
    printf("=================================================================\n");
    return 0;
}
