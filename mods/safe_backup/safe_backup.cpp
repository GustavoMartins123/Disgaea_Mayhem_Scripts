#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include "../../native/mod_loader/mod_loader_api.h"

static char g_save_dir[MAX_PATH] = {};
static char g_backup_dir[MAX_PATH] = {};
static HANDLE g_stop_event = NULL;
static HANDLE g_backup_thread = NULL;
static std::atomic<int> g_max_backups{20};

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

static void RotateBackups(const char* backup_dest_dir, const char* slot_name) {
    const int keep = g_max_backups.load(std::memory_order_acquire);
    if (keep <= 0) return;

    char pattern[MAX_PATH] = {};
    snprintf(pattern, sizeof(pattern), "%s\\%s_*.bak", backup_dest_dir, slot_name);

    WIN32_FIND_DATAA fd = {};
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    std::vector<std::string> names;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) names.push_back(fd.cFileName);
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);

    if (static_cast<int>(names.size()) <= keep) return;
    std::sort(names.begin(), names.end());

    const size_t excess = names.size() - static_cast<size_t>(keep);
    for (size_t i = 0; i < excess; ++i) {
        char victim[MAX_PATH] = {};
        snprintf(victim, sizeof(victim), "%s\\%s", backup_dest_dir, names[i].c_str());
        DeleteFileA(victim);
    }
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
                RotateBackups(backup_dest_dir, fd.cFileName);
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

    __declspec(dllexport) BOOL WINAPI Mod_SetOption(const char* key, const DmModValue* value) {
        if (key == NULL || value == NULL || value->struct_size != sizeof(DmModValue)) return FALSE;
        if (strcmp(key, "max_backups") != 0) return FALSE;
        if (value->type != DmOptionType::SliderInt ||
            value->int_value < 1 || value->int_value > 200) {
            return FALSE;
        }
        g_max_backups.store(value->int_value, std::memory_order_release);
        return TRUE;
    }

    __declspec(dllexport) void WINAPI Mod_Shutdown() {
        Mod_Disable();
    }
}
