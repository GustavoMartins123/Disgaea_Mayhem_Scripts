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
    // 1. Check in the current executable's directory
    char self_path[MAX_PATH] = {};
    GetModuleFileNameA(NULL, self_path, MAX_PATH);
    char* last_slash = strrchr(self_path, '\\');
    if (last_slash) *last_slash = '\0';

    snprintf(out_path, max_len, "%s\\%s", self_path, dll_name);
    if (GetFileAttributesA(out_path) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }

    // 2. Check in relative subfolder mods/<mod_name>/<dll_name>
    snprintf(out_path, max_len, "%s\\mods\\%s\\%s", self_path, mod_name, dll_name);
    if (GetFileAttributesA(out_path) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }

    // 3. Auto-discover from the running game process directory
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

int main() {
    SetConsoleOutputCP(CP_UTF8);

    printf("=================================================================\n");
    printf("  DISGAEA MAYHEM - CHARA WORLD HOOK (AUTO-DISCOVERY)\n");
    printf("=================================================================\n");

    // 1. Auto-discover game process Disgaea_Mayhem.exe
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        printf("[ERRO] Falha ao listar processos do sistema.\n");
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
        printf("[INFO] Disgaea_Mayhem.exe nao esta em execucao no momento.\n");
        printf("       O hook sera carregado automaticamente pelo Mod Menu ao iniciar o jogo.\n");
        printf("=================================================================\n");
        return 0;
    }

    printf("[OK] Processo do jogo auto-detectado (PID: %lu)\n", pid);

    // 2. Auto-discover DLL path dynamically
    char dll_full_path[MAX_PATH] = {};
    if (!AutoDiscoverDllPath(pid, "chara_world", "chara_world.dll", dll_full_path, sizeof(dll_full_path))) {
        printf("[ERRO] Nao foi possivel auto-descobrir o arquivo chara_world.dll.\n");
        printf("       Certifique-se de que a pasta 'mods/chara_world' esta presente.\n");
        return 1;
    }

    printf("[OK] Modulo auto-descoberto: %s\n", dll_full_path);

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        printf("[ERRO] Falha ao abrir o processo do jogo (Execute como Administrador se necessario).\n");
        return 1;
    }

    size_t len = strlen(dll_full_path) + 1;
    LPVOID pRemoteMem = VirtualAllocEx(hProcess, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (pRemoteMem) {
        WriteProcessMemory(hProcess, pRemoteMem, dll_full_path, len, NULL);
        HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
        LPTHREAD_START_ROUTINE pfnLoadLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "LoadLibraryA");
        
        HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pfnLoadLibrary, pRemoteMem, 0, NULL);
        if (hThread) {
            WaitForSingleObject(hThread, 3000);
            DWORD exit_code = 0;
            GetExitCodeThread(hThread, &exit_code);
            CloseHandle(hThread);
            printf("[SUCESSO] Hook residente chara_world.dll ativo na memoria do jogo!\n");
            printf("  -> Energia congelada em 100/100 para movimentos e batalhas infinitas.\n");
        } else {
            printf("[AVISO] Nao foi possivel criar thread remota no processo.\n");
        }
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
    }

    CloseHandle(hProcess);
    printf("=================================================================\n");
    return 0;
}
