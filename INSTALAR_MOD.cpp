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


std::string join_path(const std::string& base, const std::string& child) {
    if (base.empty()) return child;
    if (base.back() == '\\' || base.back() == '/') return base + child;
    return base + "\\" + child;
}

bool ensure_directory(const std::string& path) {
    if (path.empty() || dir_exists(path.c_str())) return true;

    size_t slash = path.find_last_of("\\/");
    if (slash != std::string::npos) {
        std::string parent = path.substr(0, slash);
        if (!parent.empty() && parent.back() != ':') {
            ensure_directory(parent);
        }
    }

    if (CreateDirectoryA(path.c_str(), NULL)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS && dir_exists(path.c_str());
}

bool copy_file_replace(const std::string& source, const std::string& target, const char* label) {
    size_t slash = target.find_last_of("\\/");
    if (slash != std::string::npos) {
        ensure_directory(target.substr(0, slash));
    }
    if (!CopyFileA(source.c_str(), target.c_str(), FALSE)) {
        printf("[ERRO] Falha ao copiar %s: %s -> %s (Win32=%lu)\\n",
               label, source.c_str(), target.c_str(), GetLastError());
        return false;
    }
    return true;
}

bool copy_directory_recursive(const std::string& source, const std::string& target, bool preserve_enabled_txt) {
    if (!dir_exists(source.c_str())) {
        printf("[AVISO] Pasta de origem ausente: %s\\n", source.c_str());
        return false;
    }
    if (!ensure_directory(target)) {
        printf("[ERRO] Nao foi possivel criar pasta: %s (Win32=%lu)\\n", target.c_str(), GetLastError());
        return false;
    }

    WIN32_FIND_DATAA fd = {};
    std::string pattern = join_path(source, "*");
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    bool ok = true;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        std::string src = join_path(source, fd.cFileName);
        std::string dst = join_path(target, fd.cFileName);

        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (!copy_directory_recursive(src, dst, preserve_enabled_txt)) ok = false;
            continue;
        }

        if (preserve_enabled_txt && _stricmp(fd.cFileName, "enabled.txt") == 0 && file_exists(dst.c_str())) {
            continue;
        }
        if (!copy_file_replace(src, dst, fd.cFileName)) ok = false;
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
    return ok;
}

bool GetInstallerDir(char* out_dir, size_t max_len) {
    DWORD len = GetModuleFileNameA(NULL, out_dir, (DWORD)max_len);
    if (len == 0 || len >= max_len) return false;
    char* slash = strrchr(out_dir, '\\');
    if (!slash) return false;
    *slash = '\0';
    return true;
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

    char installer_dir[MAX_PATH] = {};
    if (!GetInstallerDir(installer_dir, sizeof(installer_dir))) {
        printf("[ERRO] Nao foi possivel resolver a pasta do instalador.\n");
        return 1;
    }
    SetCurrentDirectoryA(installer_dir);

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
    char smoke_src[MAX_PATH] = {};
    snprintf(smoke_src, sizeof(smoke_src), "%s\\SmokeAPI\\smoke_api64.dll", installer_dir);
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
    char menu_dll_src[MAX_PATH] = {};
    snprintf(menu_dll_src, sizeof(menu_dll_src), "%s\\native\\mod_menu_overlay\\build\\DisgaeaMayhemModMenu.dll", installer_dir);
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

    // 3. Copiar todos os mods do repositorio para o jogo.
    // enabled.txt existente e preservado para nao apagar a escolha do usuario.
    char mods_target[MAX_PATH] = {};
    snprintf(mods_target, sizeof(mods_target), "%s\\mods", game_dir);
    std::string mods_source = join_path(installer_dir, "mods");
    if (copy_directory_recursive(mods_source, mods_target, true)) {
        printf("[OK] Mods sincronizados com a pasta do jogo.\n");
    }

    // Reaplica o binario do host depois da copia recursiva, garantindo que a
    // versao compilada mais recente vença qualquer copia antiga de mods/native.
    if (file_exists(menu_dll_src)) {
        copy_file_replace(menu_dll_src, proxy_target, "dxgi.dll");
        copy_file_replace(menu_dll_src, native_target_dll, "DisgaeaMayhemModMenu.dll");
    }

    // 4. SmokeAPI.config.json
    std::string smoke_config_source = join_path(installer_dir, "SmokeAPI.config.json");
    std::string smoke_config_target = join_path(game_dir, "SmokeAPI.config.json");
    if (file_exists(smoke_config_source.c_str()) &&
        copy_file_replace(smoke_config_source, smoke_config_target, "SmokeAPI.config.json")) {
        printf("[OK] SmokeAPI.config.json sincronizado.\n");
    }

    // 5. Tabelas modificadas: DLC, Cheat Shop e Dark Assembly.
    std::string database_source = join_path(installer_dir, "database_mods");
    std::string database_target = join_path(game_dir, "data\\database");
    if (copy_directory_recursive(database_source, database_target, false)) {
        printf("[OK] Tabelas database_mods aplicadas em data\\database.\n");
    }

    printf("======================================================================\n");
    printf("[SUCESSO] Modding framework instalado com sucesso via auto-discovery!\n");
    printf("======================================================================\n");
    return 0;
}
