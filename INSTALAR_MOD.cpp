#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>

bool file_exists(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

bool dir_exists(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

// -----------------------------------------------------------------------------
// Auto-Discovery: Dynamically discovers the game installation directory
// -----------------------------------------------------------------------------
bool AutoDiscoverGameDir(char* out_dir, size_t max_len) {
    // 1. Check if the current directory is already the game folder
    if (file_exists("Disgaea_Mayhem.exe")) {
        GetCurrentDirectoryA((DWORD)max_len, out_dir);
        return true;
    }

    // 2. Check parent directory
    if (file_exists("..\\Disgaea_Mayhem.exe")) {
        GetFullPathNameA("..", (DWORD)max_len, out_dir, NULL);
        return true;
    }

    // 3. Check if Disgaea_Mayhem.exe process is currently running
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe = { sizeof(pe) };
        if (Process32First(hSnap, &pe)) {
            do {
                if (_stricmp(pe.szExeFile, "Disgaea_Mayhem.exe") == 0) {
                    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
                    if (hProcess) {
                        char exe_full_path[MAX_PATH] = {};
                        if (GetModuleFileNameExA(hProcess, NULL, exe_full_path, MAX_PATH)) {
                            char* slash = strrchr(exe_full_path, '\\');
                            if (slash) *slash = '\0';
                            snprintf(out_dir, max_len, "%s", exe_full_path);
                            CloseHandle(hProcess);
                            CloseHandle(hSnap);
                            return true;
                        }
                        CloseHandle(hProcess);
                    }
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    // 4. Dynamic drive scan (A: to Z:)
    char drive_letter[] = "A:\\";
    for (char c = 'C'; c <= 'Z'; ++c) {
        drive_letter[0] = c;
        char test_path[MAX_PATH] = {};
        
        // Steam default and SteamLibrary paths
        snprintf(test_path, sizeof(test_path), "%c:\\Steam\\steamapps\\common\\Disgaea Mayhem", c);
        if (file_exists((std::string(test_path) + "\\Disgaea_Mayhem.exe").c_str())) {
            snprintf(out_dir, max_len, "%s", test_path);
            return true;
        }
        snprintf(test_path, sizeof(test_path), "%c:\\SteamLibrary\\steamapps\\common\\Disgaea Mayhem", c);
        if (file_exists((std::string(test_path) + "\\Disgaea_Mayhem.exe").c_str())) {
            snprintf(out_dir, max_len, "%s", test_path);
            return true;
        }
        snprintf(test_path, sizeof(test_path), "%c:\\Program Files (x86)\\Steam\\steamapps\\common\\Disgaea Mayhem", c);
        if (file_exists((std::string(test_path) + "\\Disgaea_Mayhem.exe").c_str())) {
            snprintf(out_dir, max_len, "%s", test_path);
            return true;
        }
    }

    return false;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    printf("======================================================================\n");
    printf("  DISGAEA MAYHEM - INSTALADOR AUTOMATICO DE MODS (AUTO-DISCOVERY)\n");
    printf("======================================================================\n");

    char game_dir[MAX_PATH] = {};
    if (!AutoDiscoverGameDir(game_dir, sizeof(game_dir))) {
        printf("[?] Digite a pasta onde fica o Disgaea_Mayhem.exe: ");
        if (!fgets(game_dir, sizeof(game_dir), stdin)) return 1;
        char* nl = strchr(game_dir, '\n'); if (nl) *nl = 0;
        char* cr = strchr(game_dir, '\r'); if (cr) *cr = 0;
    }

    printf("[OK] Pasta do jogo auto-descoberta: %s\n", game_dir);

    // 1. SmokeAPI DLL
    char smoke_src[MAX_PATH] = "SmokeAPI\\smoke_api64.dll";
    char target_dll[MAX_PATH] = {};
    char backup_dll[MAX_PATH] = {};
    snprintf(target_dll, sizeof(target_dll), "%s\\steam_api64.dll", game_dir);
    snprintf(backup_dll, sizeof(backup_dll), "%s\\steam_api64_o.dll", game_dir);

    if (file_exists(target_dll) && !file_exists(backup_dll)) {
        CopyFileA(target_dll, backup_dll, FALSE);
        printf("[OK] Backup original salvo como steam_api64_o.dll\n");
    }

    if (file_exists(smoke_src)) {
        CopyFileA(smoke_src, target_dll, FALSE);
        printf("[OK] SmokeAPI steam_api64.dll instalado.\n");
    }

    // 2. Mod Menu & Proxy DLL
    char menu_dll_src[MAX_PATH] = "native\\mod_menu_overlay\\build\\DisgaeaMayhemModMenu.dll";
    char proxy_target[MAX_PATH] = {};
    char native_target_dir[MAX_PATH] = {};
    char native_target_dll[MAX_PATH] = {};

    snprintf(proxy_target, sizeof(proxy_target), "%s\\dxgi.dll", game_dir);
    snprintf(native_target_dir, sizeof(native_target_dir), "%s\\mods\\native", game_dir);
    snprintf(native_target_dll, sizeof(native_target_dll), "%s\\DisgaeaMayhemModMenu.dll", native_target_dir);

    CreateDirectoryA(native_target_dir, NULL);

    if (file_exists(menu_dll_src)) {
        CopyFileA(menu_dll_src, proxy_target, FALSE);
        CopyFileA(menu_dll_src, native_target_dll, FALSE);
        printf("[OK] Mod Menu Overlay instalado como dxgi.dll e em mods/native/.\n");
    }

    // 3. Copiar mods
    char mods_target[MAX_PATH] = {};
    snprintf(mods_target, sizeof(mods_target), "%s\\mods", game_dir);
    CreateDirectoryA(mods_target, NULL);

    printf("======================================================================\n");
    printf("[SUCESSO] Modding framework instalado com sucesso via auto-discovery!\n");
    printf("======================================================================\n");
    return 0;
}
