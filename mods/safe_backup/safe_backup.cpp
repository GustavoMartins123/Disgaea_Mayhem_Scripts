#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// -----------------------------------------------------------------------------
// Auto-Discovery: Find Disgaea Mayhem Save Directory in AppData
// -----------------------------------------------------------------------------
static bool GetDisgaeaSaveDir(char* out_path, size_t max_len) {
    char appdata[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
        return false;
    }

    char search_pattern[MAX_PATH] = {};
    snprintf(search_pattern, sizeof(search_pattern), "%s\\Nippon Ichi Software, Inc\\Disgaea Mayhem\\*", appdata);

    WIN32_FIND_DATAA fd = {};
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
            snprintf(out_path, max_len, "%s\\Nippon Ichi Software, Inc\\Disgaea Mayhem\\%s", appdata, fd.cFileName);
            FindClose(hFind);
            return true;
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
    return false;
}

// -----------------------------------------------------------------------------
// Perform Safe Timestamped Backup
// -----------------------------------------------------------------------------
static void BackupAllSaves(const char* save_dir, const char* backup_dest_dir) {
    CreateDirectoryA(backup_dest_dir, NULL);

    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char time_str[64] = {};
    strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", t);

    char slot_pattern[MAX_PATH] = {};
    snprintf(slot_pattern, sizeof(slot_pattern), "%s\\save.*", save_dir);

    WIN32_FIND_DATAA fd = {};
    HANDLE hFind = FindFirstFileA(slot_pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !strstr(fd.cFileName, ".bak")) {
                char src_file[MAX_PATH] = {};
                char dst_file[MAX_PATH] = {};
                snprintf(src_file, sizeof(src_file), "%s\\%s", save_dir, fd.cFileName);
                snprintf(dst_file, sizeof(dst_file), "%s\\%s_%s.bak", backup_dest_dir, fd.cFileName, time_str);

                CopyFileA(src_file, dst_file, FALSE);
                printf("[BACKUP SEGURO] Criado: %s\n", dst_file);
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
}

// -----------------------------------------------------------------------------
// Background Daemon Thread
// -----------------------------------------------------------------------------
static DWORD WINAPI SaveBackupDaemonThread(LPVOID lpParam) {
    char save_dir[MAX_PATH] = {};
    if (!GetDisgaeaSaveDir(save_dir, sizeof(save_dir))) return 1;

    char self_path[MAX_PATH] = {};
    GetModuleFileNameA(NULL, self_path, MAX_PATH);
    char* last_slash = strrchr(self_path, '\\');
    if (last_slash) *last_slash = '\0';

    char backup_dir[MAX_PATH] = {};
    snprintf(backup_dir, sizeof(backup_dir), "%s\\backups", self_path);
    CreateDirectoryA(backup_dir, NULL);

    // Initial backup on startup
    BackupAllSaves(save_dir, backup_dir);

    FILETIME last_write_time = {};

    while (true) {
        Sleep(5000); // Check every 5s

        char save_file[MAX_PATH] = {};
        snprintf(save_file, sizeof(save_file), "%s\\save.002", save_dir);

        WIN32_FILE_ATTRIBUTE_DATA file_info = {};
        if (GetFileAttributesExA(save_file, GetFileExInfoStandard, &file_info)) {
            if (CompareFileTime(&last_write_time, &file_info.ftLastWriteTime) != 0) {
                last_write_time = file_info.ftLastWriteTime;
                BackupAllSaves(save_dir, backup_dir);
            }
        }
    }
    return 0;
}

// -----------------------------------------------------------------------------
// DLL Exports
// -----------------------------------------------------------------------------
extern "C" {
    __declspec(dllexport) void Mod_Enable() {
        CreateThread(NULL, 0, SaveBackupDaemonThread, NULL, 0, NULL);
    }
    __declspec(dllexport) void Mod_Disable() {}
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    printf("=================================================================\n");
    printf("  DISGAEA MAYHEM - GUARDIÃO DE BACKUP SEGURO DE SAVES (C++)\n");
    printf("=================================================================\n");

    char save_dir[MAX_PATH] = {};
    if (!GetDisgaeaSaveDir(save_dir, sizeof(save_dir))) {
        printf("[ERRO] Nao foi possivel auto-localizar a pasta de saves em AppData.\n");
        return 1;
    }

    printf("[OK] Pasta de Saves auto-descoberta:\n     %s\n\n", save_dir);

    char backup_dir[MAX_PATH] = {};
    char self_path[MAX_PATH] = {};
    GetModuleFileNameA(NULL, self_path, MAX_PATH);
    char* last_slash = strrchr(self_path, '\\');
    if (last_slash) *last_slash = '\0';
    snprintf(backup_dir, sizeof(backup_dir), "%s\\backups", self_path);
    CreateDirectoryA(backup_dir, NULL);

    BackupAllSaves(save_dir, backup_dir);

    printf("\n=================================================================\n");
    printf("[SUCESSO] Backup completo dos saves gerado em:\n  %s\n", backup_dir);
    printf("=================================================================\n");
    return 0;
}
