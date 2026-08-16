#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>

void find_game_root(char* out_path, size_t max_len) {
    char current[MAX_PATH] = {};
    GetModuleFileNameA(NULL, current, MAX_PATH);
    // Remover nome do executável
    char* last_slash = strrchr(current, '\\');
    if (last_slash) *last_slash = 0;

    // Verificar se Disgaea_Mayhem.exe está na pasta atual ou nas anteriores
    char check_path[MAX_PATH] = {};
    snprintf(check_path, sizeof(check_path), "%s\\Disgaea_Mayhem.exe", current);
    if (GetFileAttributesA(check_path) != INVALID_FILE_ATTRIBUTES) {
        snprintf(out_path, max_len, "%s", current);
        return;
    }

    snprintf(check_path, sizeof(check_path), "%s\\..\\Disgaea_Mayhem.exe", current);
    if (GetFileAttributesA(check_path) != INVALID_FILE_ATTRIBUTES) {
        snprintf(out_path, max_len, "%s\\..", current);
        return;
    }

    snprintf(check_path, sizeof(check_path), "%s\\..\\..\\Disgaea_Mayhem.exe", current);
    if (GetFileAttributesA(check_path) != INVALID_FILE_ATTRIBUTES) {
        snprintf(out_path, max_len, "%s\\..\\..", current);
        return;
    }

    snprintf(out_path, max_len, ".");
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    printf("============================================================\n");
    printf("  DISGAEA MAYHEM - MOD DE RESGATE DE DLCS (C++ NATIVE)\n");
    printf("============================================================\n");

    char game_root[MAX_PATH] = {};
    find_game_root(game_root, sizeof(game_root));

    char db_dir[MAX_PATH] = {};
    snprintf(db_dir, sizeof(db_dir), "%s\\data\\database", game_root);

    // 1. Configurar SmokeAPI.config.json
    char config_path[MAX_PATH] = {};
    snprintf(config_path, sizeof(config_path), "%s\\SmokeAPI.config.json", game_root);

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

    // 2. Patch em DLC_information.dat (converter consumíveis para Tipo 1 offline)
    char info_path[MAX_PATH] = {};
    snprintf(info_path, sizeof(info_path), "%s\\DLC_information.dat", db_dir);

    FILE* f_info = fopen(info_path, "rb");
    if (f_info) {
        fseek(f_info, 0, SEEK_END);
        long sz = ftell(f_info);
        fseek(f_info, 0, SEEK_SET);
        if (sz > 0) {
            std::vector<uint8_t> data(sz);
            fread(data.data(), 1, sz, f_info);
            fclose(f_info);

            struct DLCItem {
                const char* sym;
                uint32_t id;
                const char* item_str;
            };

            DLCItem items[] = {
                { "DLC_INFORMATION_HL", 3001, "HL1M" },
                { "DLC_INFORMATION_MANA", 3002, "Mana1M" },
                { "DLC_INFORMATION_BOOST_TICKET_100", 3003, "Boost100" },
                { "DLC_INFORMATION_BOOST_TICKET_400", 3004, "Boost400" },
                { "DLC_INFORMATION_BOOST_TICKET_900", 3005, "Boost900" },
            };

            for (const auto& it : items) {
                size_t sym_len = strlen(it.sym);
                for (size_t i = 0; i <= data.size() - sym_len; ++i) {
                    if (memcmp(data.data() + i, it.sym, sym_len) == 0) {
                        for (size_t j = i + sym_len; j <= std::min(data.size() - 4, i + 120); ++j) {
                            if (*(uint32_t*)(data.data() + j) == it.id) {
                                *(uint32_t*)(data.data() + j - 4) = 1; // Tipo 1 offline
                                break;
                            }
                        }
                    }
                }
            }

            f_info = fopen(info_path, "wb");
            if (f_info) {
                fwrite(data.data(), 1, data.size(), f_info);
                fclose(f_info);
                printf("[OK] DLC_information.dat configurado para resgate offline sem erros de rede.\n");
            }
        } else {
            fclose(f_info);
        }
    }

    // 3. Patch em DLC_BoostTicket.dat (multiplicador 9x / 900%)
    char boost_path[MAX_PATH] = {};
    snprintf(boost_path, sizeof(boost_path), "%s\\DLC_BoostTicket.dat", db_dir);

    FILE* f_boost = fopen(boost_path, "rb");
    if (f_boost) {
        fseek(f_boost, 0, SEEK_END);
        long sz = ftell(f_boost);
        fseek(f_boost, 0, SEEK_SET);
        if (sz > 0) {
            std::vector<uint8_t> b_data(sz);
            fread(b_data.data(), 1, sz, f_boost);
            fclose(f_boost);

            const char* s100 = "BOOST_TICKET_100";
            const char* s400 = "BOOST_TICKET_400";
            size_t l100 = strlen(s100);
            size_t l400 = strlen(s400);

            for (size_t i = 0; i <= b_data.size() - l100; ++i) {
                if (memcmp(b_data.data() + i, s100, l100) == 0 && i + 17 < b_data.size()) {
                    b_data[i + 17] = 0x09;
                }
            }
            for (size_t i = 0; i <= b_data.size() - l400; ++i) {
                if (memcmp(b_data.data() + i, s400, l400) == 0 && i + 17 < b_data.size()) {
                    b_data[i + 17] = 0x09;
                }
            }

            f_boost = fopen(boost_path, "wb");
            if (f_boost) {
                fwrite(b_data.data(), 1, b_data.size(), f_boost);
                fclose(f_boost);
                printf("[OK] DLC_BoostTicket.dat configurado (+900%% multiplicador em todos os tickets).\n");
            }
        } else {
            fclose(f_boost);
        }
    }

    printf("\n============================================================\n");
    printf("  TUDO PRONTO! Agora voce pode resgatar quando quiser no NPC Carlbunch.\n");
    printf("============================================================\n");
    return 0;
}
