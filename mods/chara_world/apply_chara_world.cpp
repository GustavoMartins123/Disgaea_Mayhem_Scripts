#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// -----------------------------------------------------------------------------
// Auto-Discovery: Dynamically locates the target DLL across runtime paths
// -----------------------------------------------------------------------------
static bool AutoDiscoverDllPath(DWORD pid, const char* mod_name, const char* dll_name, char* out_path, size_t max_len) {
    char self_path[MAX_PATH] = {};
    GetModuleFileNameA(NULL, self_path, MAX_PATH);
    char* last_slash = strrchr(self_path, '\\');
    if (last_slash) *last_slash = '\0';

    snprintf(out_path, max_len, "%s\\%s", self_path, dll_name);
    if (GetFileAttributesA(out_path) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }

    snprintf(out_path, max_len, "%s\\mods\\%s\\%s", self_path, mod_name, dll_name);
    if (GetFileAttributesA(out_path) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }

    if (pid != 0) {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (hProcess) {
            char game_exe_path[MAX_PATH] = {};
            if (GetModuleFileNameExA(hProcess, NULL, game_exe_path, MAX_PATH)) {
                char* game_slash = strrchr(game_exe_path, '\\');
                if (game_slash) *game_slash = '\0';

                snprintf(out_path, max_len, "%s\\mods\\%s\\%s", game_exe_path, mod_name, dll_name);
                if (GetFileAttributesA(out_path) != INVALID_FILE_ATTRIBUTES) {
                    CloseHandle(hProcess);
                    return true;
                }
            }
            CloseHandle(hProcess);
        }
    }

    return false;
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);

    bool disable_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--disable") == 0 || strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "off") == 0) {
            disable_mode = true;
        }
    }

    printf("=================================================================\n");
    printf("  DISGAEA MAYHEM - CHARA WORLD HOOK CONTROLLER\n");
    printf("=================================================================\n");

    // 1. Sync enabled.txt locally
    char self_path[MAX_PATH] = {};
    GetModuleFileNameA(NULL, self_path, MAX_PATH);
    char* last_slash = strrchr(self_path, '\\');
    if (last_slash) *last_slash = '\0';

    char enabled_path[MAX_PATH] = {};
    snprintf(enabled_path, sizeof(enabled_path), "%s\\enabled.txt", self_path);
    FILE* f_enabled = fopen(enabled_path, "w");
    if (f_enabled) {
        fputc(disable_mode ? '0' : '1', f_enabled);
        fclose(f_enabled);
    }

    // 2. Detect game process
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 1;

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
        printf("[INFO] Jogo nao esta aberto. Flag enabled.txt atualizada para %s.\n",
            disable_mode ? "0 (OFF)" : "1 (ON)");
        printf("=================================================================\n");
        return 0;
    }

    printf("[OK] Processo detectado (PID: %lu)\n", pid);

    char dll_full_path[MAX_PATH] = {};
    if (!AutoDiscoverDllPath(pid, "chara_world", "chara_world.dll", dll_full_path, sizeof(dll_full_path))) {
        printf("[ERRO] chara_world.dll nao encontrada.\n");
        return 1;
    }

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        printf("[ERRO] Falha ao abrir o processo do jogo.\n");
        return 1;
    }

    // Check if chara_world.dll is loaded in the game
    HMODULE hMods[1024];
    DWORD cbNeeded = 0;
    HMODULE hTargetMod = NULL;

    if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
        for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); ++i) {
            char szModName[MAX_PATH] = {};
            if (GetModuleBaseNameA(hProcess, hMods[i], szModName, sizeof(szModName))) {
                if (_stricmp(szModName, "chara_world.dll") == 0) {
                    hTargetMod = hMods[i];
                    break;
                }
            }
        }
    }

    if (disable_mode) {
        if (hTargetMod) {
            // Get offset of Mod_Disable
            HMODULE hLocalMod = LoadLibraryA(dll_full_path);
            if (hLocalMod) {
                FARPROC pfnLocal = GetProcAddress(hLocalMod, "Mod_Disable");
                uintptr_t offset = (uintptr_t)pfnLocal - (uintptr_t)hLocalMod;
                LPTHREAD_START_ROUTINE pfnRemote = (LPTHREAD_START_ROUTINE)((uintptr_t)hTargetMod + offset);

                HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pfnRemote, NULL, 0, NULL);
                if (hThread) {
                    WaitForSingleObject(hThread, 2000);
                    CloseHandle(hThread);
                    printf("[SUCESSO] Mod Chara World DESATIVADO (OFF) com sucesso na memoria do jogo!\n");
                }
                FreeLibrary(hLocalMod);
            }
        } else {
            printf("[OK] Mod ja esta desativado (nao carregado no jogo).\n");
        }
    } else {
        if (!hTargetMod) {
            // Load chara_world.dll
            size_t len = strlen(dll_full_path) + 1;
            LPVOID pRemoteMem = VirtualAllocEx(hProcess, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (pRemoteMem) {
                WriteProcessMemory(hProcess, pRemoteMem, dll_full_path, len, NULL);
                HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
                LPTHREAD_START_ROUTINE pfnLoadLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "LoadLibraryA");
                
                HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pfnLoadLibrary, pRemoteMem, 0, NULL);
                if (hThread) {
                    WaitForSingleObject(hThread, 3000);
                    CloseHandle(hThread);
                }
                VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
            }
        } else {
            // Call Mod_Enable
            HMODULE hLocalMod = LoadLibraryA(dll_full_path);
            if (hLocalMod) {
                FARPROC pfnLocal = GetProcAddress(hLocalMod, "Mod_Enable");
                uintptr_t offset = (uintptr_t)pfnLocal - (uintptr_t)hLocalMod;
                LPTHREAD_START_ROUTINE pfnRemote = (LPTHREAD_START_ROUTINE)((uintptr_t)hTargetMod + offset);

                HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pfnRemote, NULL, 0, NULL);
                if (hThread) {
                    WaitForSingleObject(hThread, 2000);
                    CloseHandle(hThread);
                }
                FreeLibrary(hLocalMod);
            }
        }
        printf("[SUCESSO] Mod Chara World ATIVADO (ON) na memoria do jogo!\n");
        printf("  -> Energia congelada em 100/100 para movimentos e batalhas infinitas.\n");
    }

    CloseHandle(hProcess);
    printf("=================================================================\n");
    return 0;
}
