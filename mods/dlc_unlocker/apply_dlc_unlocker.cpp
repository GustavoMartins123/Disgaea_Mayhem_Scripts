#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static bool FileExists(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool FindGameRoot(std::string& game_root) {
    char executable_path[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, executable_path, MAX_PATH)) return false;
    char* separator = std::strrchr(executable_path, '\\');
    if (separator == nullptr) return false;
    *separator = '\0';

    char relative_root[MAX_PATH] = {};
    std::snprintf(relative_root, sizeof(relative_root), "%s\\..\\..", executable_path);
    char resolved_root[MAX_PATH] = {};
    if (!GetFullPathNameA(relative_root, MAX_PATH, resolved_root, nullptr)) return false;
    const std::string game_executable = std::string(resolved_root) + "\\Disgaea_Mayhem.exe";
    if (!FileExists(game_executable)) return false;
    game_root = resolved_root;
    return true;
}

static bool ReadFile(const std::string& path, std::vector<std::uint8_t>& data) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return false;
    }
    const long length = std::ftell(file);
    if (length <= 0) {
        std::fclose(file);
        return false;
    }
    std::rewind(file);
    data.resize(static_cast<std::size_t>(length));
    const bool read = std::fread(data.data(), 1, data.size(), file) == data.size();
    std::fclose(file);
    return read;
}

static bool AtomicWrite(const std::string& path, const std::vector<std::uint8_t>& data) {
    const std::string temporary = path + ".dm-action.tmp";
    HANDLE file = CreateFileA(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = data.size() <= MAXDWORD &&
                    WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) != FALSE &&
                    written == data.size() && FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (!ok || !MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(temporary.c_str());
        return false;
    }
    return true;
}

static bool ReadU32(const std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t& value) {
    if (offset + sizeof(value) > data.size()) return false;
    std::memcpy(&value, data.data() + offset, sizeof(value));
    return true;
}

static bool PatchDlcInformation(std::vector<std::uint8_t>& data) {
    struct Item {
        const char* symbol;
        std::uint32_t id;
    };
    const Item items[] = {
        {"DLC_INFORMATION_HL", 3001},
        {"DLC_INFORMATION_MANA", 3002},
        {"DLC_INFORMATION_BOOST_TICKET_100", 3003},
        {"DLC_INFORMATION_BOOST_TICKET_400", 3004},
        {"DLC_INFORMATION_BOOST_TICKET_900", 3005},
    };

    for (const Item& item : items) {
        const std::size_t symbol_length = std::strlen(item.symbol);
        bool patched = false;
        if (data.size() < symbol_length) return false;
        for (std::size_t offset = 0; offset + symbol_length <= data.size() && !patched; ++offset) {
            if (std::memcmp(data.data() + offset, item.symbol, symbol_length) != 0) continue;
            const std::size_t search_begin = offset + symbol_length;
            const std::size_t search_end = std::min(data.size(), offset + 120);
            for (std::size_t cursor = search_begin; cursor + 4 <= search_end; ++cursor) {
                std::uint32_t id = 0;
                if (ReadU32(data, cursor, id) && id == item.id && cursor >= 4) {
                    const std::uint32_t offline_type = 1;
                    std::memcpy(data.data() + cursor - 4, &offline_type, sizeof(offline_type));
                    patched = true;
                    break;
                }
            }
        }
        if (!patched) return false;
    }
    return true;
}

static bool PatchBoostTickets(std::vector<std::uint8_t>& data) {
    const char* symbols[] = {"BOOST_TICKET_100", "BOOST_TICKET_400"};
    for (const char* symbol : symbols) {
        const std::size_t length = std::strlen(symbol);
        bool patched = false;
        if (data.size() < length + 18) return false;
        for (std::size_t offset = 0; offset + length + 18 <= data.size(); ++offset) {
            if (std::memcmp(data.data() + offset, symbol, length) == 0) {
                data[offset + 17] = 0x09;
                patched = true;
                break;
            }
        }
        if (!patched) return false;
    }
    return true;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::string game_root;
    if (!FindGameRoot(game_root)) {
        std::fprintf(stderr, "[ERRO] Estrutura canonica mods/dlc_unlocker nao encontrada.\n");
        return 1;
    }

    const std::string config_path = game_root + "\\SmokeAPI.config.json";
    const std::string info_path = game_root + "\\data\\database\\DLC_information.dat";
    const std::string boost_path = game_root + "\\data\\database\\DLC_BoostTicket.dat";
    std::vector<std::uint8_t> original_config;
    std::vector<std::uint8_t> original_info;
    std::vector<std::uint8_t> original_boost;
    if (!ReadFile(config_path, original_config) || !ReadFile(info_path, original_info) ||
        !ReadFile(boost_path, original_boost)) {
        std::fprintf(stderr, "[ERRO] Um arquivo canonico do DLC Unlocker esta ausente ou ilegivel.\n");
        return 2;
    }

    std::vector<std::uint8_t> patched_info = original_info;
    std::vector<std::uint8_t> patched_boost = original_boost;
    if (!PatchDlcInformation(patched_info) || !PatchBoostTickets(patched_boost)) {
        std::fprintf(stderr, "[ERRO] Tabelas nao correspondem a build esperada; nenhuma gravacao realizada.\n");
        return 3;
    }

    static const char smoke_config[] =
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
    const std::vector<std::uint8_t> patched_config(smoke_config, smoke_config + sizeof(smoke_config) - 1);

    if (!AtomicWrite(config_path, patched_config)) return 4;
    if (!AtomicWrite(info_path, patched_info)) {
        AtomicWrite(config_path, original_config);
        return 5;
    }
    if (!AtomicWrite(boost_path, patched_boost)) {
        AtomicWrite(info_path, original_info);
        AtomicWrite(config_path, original_config);
        return 6;
    }

    std::printf("[OK] DLC Unlocker aplicado de forma transacional.\n");
    return 0;
}
