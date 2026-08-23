#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../native/mod_loader/mod_loader_api.h"

static char g_save_dir[MAX_PATH] = {};
static char g_backup_dir[MAX_PATH] = {};
static HANDLE g_stop_event = NULL;
static HANDLE g_backup_thread = NULL;

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
    (void)lpParam;

    // Initial backup on startup
    BackupAllSaves(g_save_dir, g_backup_dir);

    FILETIME last_write_time = {};

    while (WaitForSingleObject(g_stop_event, 5000) == WAIT_TIMEOUT) {

        char save_file[MAX_PATH] = {};
        snprintf(save_file, sizeof(save_file), "%s\\save.002", g_save_dir);

        WIN32_FILE_ATTRIBUTE_DATA file_info = {};
        if (GetFileAttributesExA(save_file, GetFileExInfoStandard, &file_info)) {
            if (CompareFileTime(&last_write_time, &file_info.ftLastWriteTime) != 0) {
                last_write_time = file_info.ftLastWriteTime;
                BackupAllSaves(g_save_dir, g_backup_dir);
            }
        }
    }
    return 0;
}

// -----------------------------------------------------------------------------
// DLL Exports
// -----------------------------------------------------------------------------
extern "C" {
    __declspec(dllexport) uint32_t WINAPI Mod_GetAbiVersion() {
        return DM_MOD_LOADER_ABI_VERSION;
    }

    __declspec(dllexport) BOOL WINAPI Mod_Initialize(const DmModHostContext* context) {
        if (context == NULL || context->struct_size != sizeof(DmModHostContext) ||
            context->abi_version != DM_MOD_LOADER_ABI_VERSION || context->mod_directory == NULL ||
            context->loader == NULL) {
            return FALSE;
        }
        if (!GetDisgaeaSaveDir(g_save_dir, sizeof(g_save_dir))) {
            if (context->loader->Log != NULL) {
                context->loader->Log("safe_backup", "Pasta de saves nao encontrada; plugin bloqueado.");
            }
            return FALSE;
        }
        const int length = snprintf(g_backup_dir, sizeof(g_backup_dir), "%s\\backups", context->mod_directory);
        if (length <= 0 || static_cast<size_t>(length) >= sizeof(g_backup_dir)) return FALSE;
        if (!CreateDirectoryA(g_backup_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;
        return TRUE;
    }

    __declspec(dllexport) BOOL WINAPI Mod_Enable() {
        if (g_backup_thread != NULL) return TRUE;
        g_stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (g_stop_event == NULL) return FALSE;
        g_backup_thread = CreateThread(NULL, 0, SaveBackupDaemonThread, NULL, 0, NULL);
        if (g_backup_thread == NULL) {
            CloseHandle(g_stop_event);
            g_stop_event = NULL;
            return FALSE;
        }
        return TRUE;
    }

    __declspec(dllexport) BOOL WINAPI Mod_Disable() {
        if (g_backup_thread == NULL) return TRUE;
        SetEvent(g_stop_event);
        if (WaitForSingleObject(g_backup_thread, 2000) != WAIT_OBJECT_0) return FALSE;
        CloseHandle(g_backup_thread);
        CloseHandle(g_stop_event);
        g_backup_thread = NULL;
        g_stop_event = NULL;
        return TRUE;
    }

    __declspec(dllexport) BOOL WINAPI Mod_SetOption(const char*, const DmModValue*) {
        return FALSE;
    }

    __declspec(dllexport) void WINAPI Mod_Shutdown() {
        Mod_Disable();
    }
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
