#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool file_exists(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

bool dir_exists(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    printf("======================================================================\n");
    printf("  DISGAEA MAYHEM - INSTALADOR AUTOMATICO DE MODS (C++ NATIVE)\n");
    printf("======================================================================\n");

    const char* default_paths[] = {
        "E:\\Steam\\steamapps\\common\\Disgaea Mayhem",
        "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Disgaea Mayhem",
        "D:\\Steam\\steamapps\\common\\Disgaea Mayhem",
        "D:\\SteamLibrary\\steamapps\\common\\Disgaea Mayhem",
        "E:\\SteamLibrary\\steamapps\\common\\Disgaea Mayhem",
        "."
    };

    char game_dir[MAX_PATH] = {};
    for (int i = 0; i < 6; ++i) {
        char test_exe[MAX_PATH] = {};
        snprintf(test_exe, sizeof(test_exe), "%s\\Disgaea_Mayhem.exe", default_paths[i]);
        if (file_exists(test_exe)) {
            snprintf(game_dir, sizeof(game_dir), "%s", default_paths[i]);
            break;
        }
    }

    if (game_dir[0] == 0) {
        printf("[?] Digite a pasta onde fica o Disgaea_Mayhem.exe: ");
        if (!fgets(game_dir, sizeof(game_dir), stdin)) return 1;
        char* nl = strchr(game_dir, '\n'); if (nl) *nl = 0;
        char* cr = strchr(game_dir, '\r'); if (cr) *cr = 0;
    }

    printf("[OK] Pasta do jogo: %s\n", game_dir);

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
        if (CopyFileA(smoke_src, target_dll, FALSE)) {
            printf("[OK] SmokeAPI DLL instalada como steam_api64.dll\n");
        } else {
            printf("[AVISO] O jogo esta em execucao! Feche o jogo para atualizar steam_api64.dll\n");
        }
    }

    // 2. SmokeAPI.config.json
    char config_path[MAX_PATH] = {};
    snprintf(config_path, sizeof(config_path), "%s\\SmokeAPI.config.json", game_dir);
    const char* smoke_config = 
        "{\n"
        "  \"$schema\": \"https://raw.githubusercontent.com/acidicoala/SmokeAPI/refs/tags/v4.0.0/res/SmokeAPI.schema.json\",\n"
        "  \"$version\": 4,\n"
        "  \"logging\": false,\n"
        "  \"log_steam_http\": false,\n"
        "  \"default_app_status\": \"unlocked\",\n"
        "  \"override_app_status\": {},\n"
        "  \"override_dlc_status\": {},\n"
        "  \"auto_inject_inventory\": true,\n"
        "  \"extra_inventory_items\": [1, 2, 3, 4, 5],\n"
        "  \"extra_dlcs\": {}\n"
        "}\n";

    FILE* f_cfg = fopen(config_path, "wb");
    if (f_cfg) {
        fwrite(smoke_config, 1, strlen(smoke_config), f_cfg);
        fclose(f_cfg);
        printf("[OK] SmokeAPI.config.json configurado com sucesso.\n");
    }

    // 3. Database Mods
    char db_dir[MAX_PATH] = {};
    char db_backup[MAX_PATH] = {};
    snprintf(db_dir, sizeof(db_dir), "%s\\data\\database", game_dir);
    snprintf(db_backup, sizeof(db_backup), "%s\\data\\database_backup", game_dir);
    CreateDirectoryA(db_backup, NULL);

    const char* dat_files[] = { "DLC_information.dat", "DLC_BoostTicket.dat", "DLC_delivery.dat" };
    for (int i = 0; i < 3; ++i) {
        char src[MAX_PATH] = {};
        char dst[MAX_PATH] = {};
        char bak[MAX_PATH] = {};
        snprintf(src, sizeof(src), "database_mods\\%s", dat_files[i]);
        snprintf(dst, sizeof(dst), "%s\\%s", db_dir, dat_files[i]);
        snprintf(bak, sizeof(bak), "%s\\%s", db_backup, dat_files[i]);

        if (file_exists(dst) && !file_exists(bak)) {
            CopyFileA(dst, bak, FALSE);
        }
        if (file_exists(src)) {
            CopyFileA(src, dst, FALSE);
            printf("[OK] Banco de dados atualizado: %s\n", dat_files[i]);
        }
    }

    printf("\n======================================================================\n");
    printf("  INSTALACAO CONCLUIDA COM SUCESSO!\n");
    printf("======================================================================\n");
    return 0;
}
