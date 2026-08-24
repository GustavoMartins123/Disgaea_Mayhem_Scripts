#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "mod_loader_api.h"
#include "mod_loader_internal.h"

#include "MinHook.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct JsonSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct ModRecord {
    DmModView view = {};
    std::wstring directory_path;
    std::wstring plugin_path;
    std::wstring executable_path;
    std::string directory_utf8;
    std::string directory_path_utf8;
    std::string action_success_status;
    bool auto_apply = false;
    int load_order = 100;
    DmModValue persisted_values[DM_MAX_MOD_OPTIONS] = {};
    bool config_dirty = false;
    HMODULE module = nullptr;
    DmModEnableFn enable = nullptr;
    DmModDisableFn disable = nullptr;
    DmModSetOptionFn set_option = nullptr;
    DmModShutdownFn shutdown = nullptr;
};

constexpr DWORD kExpectedGameTimestamp = 0x6A6AB373;
constexpr DWORD kExpectedGameImageSize = 0x00E01000;

struct HookEntry {
    void* target = nullptr;
    std::string owner;
};

SRWLOCK g_records_lock = SRWLOCK_INIT;
SRWLOCK g_log_lock = SRWLOCK_INIT;
SRWLOCK g_lifecycle_lock = SRWLOCK_INIT;
SRWLOCK g_hooks_lock = SRWLOCK_INIT;
std::vector<HookEntry> g_hooks;
bool g_minhook_ready = false;
std::vector<ModRecord> g_records;
std::uint32_t g_discovery_errors = 0;
std::wstring g_game_directory;
std::string g_game_directory_utf8;
std::uintptr_t g_game_module_base = 0;
std::size_t g_game_module_size = 0;
bool g_game_build_verified = false;
FILE* g_log_file = nullptr;

template <std::size_t N>
void CopyText(char (&destination)[N], const std::string& source) {
    std::snprintf(destination, N, "%s", source.c_str());
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(),
                                         static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(),
                            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) != size) {
        return {};
    }
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(),
                            static_cast<int>(value.size()), result.data(), size) != size) {
        return {};
    }
    return result;
}

void LogLine(const char* component, const char* format, ...) {
    char message[1536] = {};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);

    AcquireSRWLockExclusive(&g_log_lock);
    if (g_log_file != nullptr) {
        SYSTEMTIME now = {};
        GetLocalTime(&now);
        std::fprintf(g_log_file, "[%02u:%02u:%02u.%03u][T%lu][%s] %s\n",
                     now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
                     GetCurrentThreadId(), component != nullptr ? component : "unknown", message);
        std::fflush(g_log_file);
    }
    OutputDebugStringA("[DisgaeaModLoader] ");
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
    ReleaseSRWLockExclusive(&g_log_lock);
}

void SetRecordStatus(ModRecord& record, DmModState state, const char* format, ...) {
    char message[sizeof(record.view.status)] = {};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);

    AcquireSRWLockExclusive(&g_records_lock);
    record.view.state = state;
    std::snprintf(record.view.status, sizeof(record.view.status), "%s", message);
    ReleaseSRWLockExclusive(&g_records_lock);
}

void SkipWhitespace(const std::string& json, std::size_t& position) {
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position])) != 0) {
        ++position;
    }
}

bool ParseStringToken(const std::string& json, std::size_t& position, std::string* decoded) {
    SkipWhitespace(json, position);
    if (position >= json.size() || json[position] != '"') return false;
    ++position;
    std::string value;
    while (position < json.size()) {
        const char current = json[position++];
        if (current == '"') {
            if (decoded != nullptr) *decoded = value;
            return true;
        }
        if (current == '\\') {
            if (position >= json.size()) return false;
            const char escaped = json[position++];
            switch (escaped) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: return false;
            }
        } else {
            value.push_back(current);
        }
    }
    return false;
}

bool SkipJsonValue(const std::string& json, std::size_t& position) {
    SkipWhitespace(json, position);
    if (position >= json.size()) return false;
    if (json[position] == '"') return ParseStringToken(json, position, nullptr);

    if (json[position] == '{') {
        ++position;
        SkipWhitespace(json, position);
        if (position < json.size() && json[position] == '}') {
            ++position;
            return true;
        }
        while (position < json.size()) {
            if (!ParseStringToken(json, position, nullptr)) return false;
            SkipWhitespace(json, position);
            if (position >= json.size() || json[position++] != ':') return false;
            if (!SkipJsonValue(json, position)) return false;
            SkipWhitespace(json, position);
            if (position < json.size() && json[position] == ',') {
                ++position;
                continue;
            }
            if (position < json.size() && json[position] == '}') {
                ++position;
                return true;
            }
            return false;
        }
        return false;
    }

    if (json[position] == '[') {
        ++position;
        SkipWhitespace(json, position);
        if (position < json.size() && json[position] == ']') {
            ++position;
            return true;
        }
        while (position < json.size()) {
            if (!SkipJsonValue(json, position)) return false;
            SkipWhitespace(json, position);
            if (position < json.size() && json[position] == ',') {
                ++position;
                continue;
            }
            if (position < json.size() && json[position] == ']') {
                ++position;
                return true;
            }
            return false;
        }
        return false;
    }

    const std::size_t start = position;
    while (position < json.size() && json[position] != ',' && json[position] != '}' &&
           json[position] != ']' && std::isspace(static_cast<unsigned char>(json[position])) == 0) {
        ++position;
    }
    return position > start;
}

bool FindObjectMember(const std::string& json, const std::string& key, JsonSpan& span) {
    std::size_t position = 0;
    SkipWhitespace(json, position);
    if (position >= json.size() || json[position++] != '{') return false;
    while (position < json.size()) {
        SkipWhitespace(json, position);
        if (position < json.size() && json[position] == '}') return false;
        std::string member_name;
        if (!ParseStringToken(json, position, &member_name)) return false;
        SkipWhitespace(json, position);
        if (position >= json.size() || json[position++] != ':') return false;
        SkipWhitespace(json, position);
        const std::size_t value_begin = position;
        if (!SkipJsonValue(json, position)) return false;
        if (member_name == key) {
            span.begin = value_begin;
            span.end = position;
            return true;
        }
        SkipWhitespace(json, position);
        if (position < json.size() && json[position] == ',') {
            ++position;
            continue;
        }
        if (position < json.size() && json[position] == '}') return false;
        return false;
    }
    return false;
}

bool ParseObjectMembers(const std::string& json,
                        std::vector<std::pair<std::string, JsonSpan>>& members) {
    std::size_t position = 0;
    SkipWhitespace(json, position);
    if (position >= json.size() || json[position++] != '{') return false;
    SkipWhitespace(json, position);
    if (position < json.size() && json[position] == '}') {
        ++position;
        SkipWhitespace(json, position);
        return position == json.size();
    }

    while (position < json.size()) {
        std::string name;
        if (!ParseStringToken(json, position, &name)) return false;
        SkipWhitespace(json, position);
        if (position >= json.size() || json[position++] != ':') return false;
        SkipWhitespace(json, position);
        JsonSpan span = {position, position};
        if (!SkipJsonValue(json, position)) return false;
        span.end = position;
        members.emplace_back(std::move(name), span);
        SkipWhitespace(json, position);
        if (position < json.size() && json[position] == ',') {
            ++position;
            continue;
        }
        if (position < json.size() && json[position] == '}') {
            ++position;
            SkipWhitespace(json, position);
            return position == json.size();
        }
        return false;
    }
    return false;
}

bool GetJsonString(const std::string& json, const char* key, std::string& value) {
    JsonSpan span;
    if (!FindObjectMember(json, key, span)) return false;
    std::size_t position = span.begin;
    return ParseStringToken(json, position, &value) && position == span.end;
}

bool GetJsonBool(const std::string& json, const char* key, bool& value) {
    JsonSpan span;
    if (!FindObjectMember(json, key, span)) return false;
    const std::string token = json.substr(span.begin, span.end - span.begin);
    if (token == "true") {
        value = true;
        return true;
    }
    if (token == "false") {
        value = false;
        return true;
    }
    return false;
}

bool GetJsonInt(const std::string& json, const char* key, int& value) {
    JsonSpan span;
    if (!FindObjectMember(json, key, span)) return false;
    const std::string token = json.substr(span.begin, span.end - span.begin);
    char* end = nullptr;
    const long parsed = std::strtol(token.c_str(), &end, 10);
    if (end == token.c_str() || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX) return false;
    value = static_cast<int>(parsed);
    return true;
}

bool GetJsonFloat(const std::string& json, const char* key, float& value) {
    JsonSpan span;
    if (!FindObjectMember(json, key, span)) return false;
    const std::string token = json.substr(span.begin, span.end - span.begin);
    char* end = nullptr;
    const float parsed = std::strtof(token.c_str(), &end);
    if (end == token.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
    value = parsed;
    return true;
}

bool ReadTextFile(const std::wstring& path, std::string& content) {
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (file == nullptr) return false;
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return false;
    }
    const long length = std::ftell(file);
    if (length <= 0 || length > 1024 * 1024) {
        std::fclose(file);
        return false;
    }
    std::rewind(file);
    content.resize(static_cast<std::size_t>(length));
    const bool ok = std::fread(content.data(), 1, content.size(), file) == content.size();
    std::fclose(file);
    return ok;
}

bool IsPlainFileName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." &&
           name.find('/') == std::string::npos && name.find('\\') == std::string::npos &&
           name.find(':') == std::string::npos;
}

bool IsStableIdentifier(const std::string& value) {
    if (value.empty()) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '_' || character == '-';
    });
}

bool HasExtension(const std::string& name, const char* extension) {
    const std::size_t extension_length = std::strlen(extension);
    return name.size() > extension_length &&
           _stricmp(name.c_str() + name.size() - extension_length, extension) == 0;
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool ReadEnabledFlag(const std::wstring& directory, bool& enabled) {
    const std::wstring path = directory + L"\\enabled.txt";
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (file == nullptr) return false;
    char content[4] = {};
    const std::size_t length = std::fread(content, 1, sizeof(content), file);
    std::fclose(file);
    const bool valid_length = length == 1 || (length == 2 && content[1] == '\n') ||
                              (length == 3 && content[1] == '\r' && content[2] == '\n');
    if (!valid_length || (content[0] != '0' && content[0] != '1')) {
        return false;
    }
    enabled = content[0] == '1';
    return true;
}

bool PersistEnabledFlag(const ModRecord& record, bool enabled) {
    const std::wstring target = record.directory_path + L"\\enabled.txt";
    const std::wstring temporary = record.directory_path + L"\\enabled.txt.tmp";
    FILE* file = _wfopen(temporary.c_str(), L"wb");
    if (file == nullptr) return false;
    bool written = std::fputc(enabled ? '1' : '0', file) != EOF;
    if (std::fflush(file) != 0) written = false;
    if (std::fclose(file) != 0) written = false;
    if (!written) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

bool ParseOptions(const std::string& manifest, ModRecord& record, std::string& error) {
    JsonSpan options_span;
    if (!FindObjectMember(manifest, "options", options_span)) return true;
    const std::string array = manifest.substr(options_span.begin, options_span.end - options_span.begin);
    if (array.empty() || array.front() != '[' || array.back() != ']') {
        error = "options deve ser um array";
        return false;
    }

    std::size_t position = 1;
    while (position + 1 < array.size()) {
        SkipWhitespace(array, position);
        if (position < array.size() && array[position] == ']') break;
        if (record.view.option_count >= DM_MAX_MOD_OPTIONS) {
            error = "options excede o limite da ABI";
            return false;
        }
        const std::size_t object_begin = position;
        if (!SkipJsonValue(array, position) || object_begin >= array.size() || array[object_begin] != '{') {
            error = "objeto invalido em options";
            return false;
        }
        const std::string option_json = array.substr(object_begin, position - object_begin);
        std::string id;
        std::string name;
        std::string type;
        if (!GetJsonString(option_json, "id", id) || !GetJsonString(option_json, "name", name) ||
            !GetJsonString(option_json, "type", type) || !IsStableIdentifier(id) || name.empty()) {
            error = "option exige id, name e type";
            return false;
        }
        if (id.size() >= sizeof(record.view.options[0].id) ||
            name.size() >= sizeof(record.view.options[0].name)) {
            error = "id ou name de option excede a ABI";
            return false;
        }
        for (std::uint32_t existing = 0; existing < record.view.option_count; ++existing) {
            if (id == record.view.options[existing].id) {
                error = "id de option duplicado";
                return false;
            }
        }

        DmModOptionView& option = record.view.options[record.view.option_count];
        option.struct_size = sizeof(option);
        option.value.struct_size = sizeof(option.value);
        CopyText(option.id, id);
        CopyText(option.name, name);

        if (type == "toggle") {
            option.type = DmOptionType::Toggle;
            option.value.type = option.type;
        } else if (type == "slider_int") {
            int minimum = 0;
            int maximum = 0;
            if (!GetJsonInt(option_json, "min", minimum) || !GetJsonInt(option_json, "max", maximum) ||
                minimum > maximum) {
                error = "option slider_int exige min/max coerentes";
                return false;
            }
            option.type = DmOptionType::SliderInt;
            option.value.type = option.type;
            option.min_int = minimum;
            option.max_int = maximum;
        } else if (type == "slider_float") {
            float minimum = 0.0f;
            float maximum = 0.0f;
            if (!GetJsonFloat(option_json, "min", minimum) || !GetJsonFloat(option_json, "max", maximum) ||
                minimum > maximum) {
                error = "option slider_float exige min/max coerentes";
                return false;
            }
            option.type = DmOptionType::SliderFloat;
            option.value.type = option.type;
            option.min_float = minimum;
            option.max_float = maximum;
        } else {
            error = "type de option desconhecido";
            return false;
        }
        ++record.view.option_count;

        SkipWhitespace(array, position);
        if (position < array.size() && array[position] == ',') {
            ++position;
            continue;
        }
        if (position < array.size() && array[position] == ']') break;
        error = "separador invalido em options";
        return false;
    }
    return true;
}

bool ParseConfig(ModRecord& record, std::string& error) {
    const std::wstring config_path = record.directory_path + L"\\config.json";
    std::string config;
    if (!ReadTextFile(config_path, config)) {
        error = "config.json ausente ou ilegivel";
        return false;
    }

    std::vector<std::pair<std::string, JsonSpan>> root_members;
    if (!ParseObjectMembers(config, root_members)) {
        error = "config.json nao e um objeto JSON valido";
        return false;
    }
    bool saw_schema = false;
    bool saw_mod_id = false;
    bool saw_options = false;
    for (const auto& member : root_members) {
        bool* seen = nullptr;
        if (member.first == "schema_version") seen = &saw_schema;
        else if (member.first == "mod_id") seen = &saw_mod_id;
        else if (member.first == "options") seen = &saw_options;
        else {
            error = "campo desconhecido em config.json: " + member.first;
            return false;
        }
        if (*seen) {
            error = "campo duplicado em config.json: " + member.first;
            return false;
        }
        *seen = true;
    }
    int schema_version = 0;
    std::string mod_id;
    JsonSpan options_span;
    if (!saw_schema || !saw_mod_id || !saw_options ||
        !GetJsonInt(config, "schema_version", schema_version) || schema_version != 1 ||
        !GetJsonString(config, "mod_id", mod_id) || mod_id != record.view.id ||
        !FindObjectMember(config, "options", options_span)) {
        error = "config.json exige schema_version=1, mod_id exato e options";
        return false;
    }

    const std::string options_json = config.substr(options_span.begin, options_span.end - options_span.begin);
    std::vector<std::pair<std::string, JsonSpan>> option_members;
    if (!ParseObjectMembers(options_json, option_members)) {
        error = "options de config.json deve ser um objeto valido";
        return false;
    }
    bool populated[DM_MAX_MOD_OPTIONS] = {};
    for (const auto& member : option_members) {
        DmModOptionView* selected = nullptr;
        std::uint32_t selected_index = 0;
        for (std::uint32_t index = 0; index < record.view.option_count; ++index) {
            if (member.first == record.view.options[index].id) {
                selected = &record.view.options[index];
                selected_index = index;
                break;
            }
        }
        if (selected == nullptr) {
            error = "option desconhecida em config.json: " + member.first;
            return false;
        }
        if (populated[selected_index]) {
            error = "option duplicada em config.json: " + member.first;
            return false;
        }
        populated[selected_index] = true;
        const std::string token = options_json.substr(member.second.begin, member.second.end - member.second.begin);
        if (selected->type == DmOptionType::Toggle) {
            if (token == "true") selected->value.bool_value = TRUE;
            else if (token == "false") selected->value.bool_value = FALSE;
            else {
                error = "option toggle exige booleano em config.json: " + member.first;
                return false;
            }
        } else if (selected->type == DmOptionType::SliderInt) {
            char* end = nullptr;
            const long parsed = std::strtol(token.c_str(), &end, 10);
            if (end == token.c_str() || *end != '\0' || parsed < selected->min_int || parsed > selected->max_int) {
                error = "option inteira fora do intervalo em config.json: " + member.first;
                return false;
            }
            selected->value.int_value = static_cast<int>(parsed);
        } else if (selected->type == DmOptionType::SliderFloat) {
            char* end = nullptr;
            const float parsed = std::strtof(token.c_str(), &end);
            if (end == token.c_str() || *end != '\0' || !std::isfinite(parsed) ||
                parsed < selected->min_float || parsed > selected->max_float) {
                error = "option decimal fora do intervalo em config.json: " + member.first;
                return false;
            }
            selected->value.float_value = parsed;
        }
    }
    for (std::uint32_t index = 0; index < record.view.option_count; ++index) {
        if (!populated[index]) {
            error = "option ausente em config.json: " + std::string(record.view.options[index].id);
            return false;
        }
    }
    return true;
}

bool PersistConfig(const ModRecord& record) {
    DmModView view = {};
    AcquireSRWLockShared(&g_records_lock);
    view = record.view;
    ReleaseSRWLockShared(&g_records_lock);

    std::ostringstream output;
    output.imbue(std::locale::classic());
    output.precision(std::numeric_limits<float>::max_digits10);
    output << "{\n  \"schema_version\": 1,\n  \"mod_id\": \"" << view.id << "\",\n  \"options\": {";
    for (std::uint32_t index = 0; index < view.option_count; ++index) {
        const DmModOptionView& option = view.options[index];
        output << (index == 0 ? "\n" : ",\n") << "    \"" << option.id << "\": ";
        if (option.type == DmOptionType::Toggle) output << (option.value.bool_value != FALSE ? "true" : "false");
        else if (option.type == DmOptionType::SliderInt) output << option.value.int_value;
        else if (option.type == DmOptionType::SliderFloat) output << option.value.float_value;
        else return false;
    }
    if (view.option_count != 0) output << '\n';
    output << "  }\n}\n";
    if (!output.good()) return false;
    const std::string content = output.str();

    const std::wstring target = record.directory_path + L"\\config.json";
    const std::wstring temporary = record.directory_path + L"\\config.json.tmp";
    FILE* file = _wfopen(temporary.c_str(), L"wb");
    if (file == nullptr) return false;
    bool written = std::fwrite(content.data(), 1, content.size(), file) == content.size();
    if (std::fflush(file) != 0) written = false;
    if (std::fclose(file) != 0) written = false;
    if (!written) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

bool ParseManifest(const std::wstring& directory_path, const std::wstring& directory_name,
                   ModRecord& record, std::string& error) {
    const std::wstring manifest_path = directory_path + L"\\mod.json";
    std::string manifest;
    if (!ReadTextFile(manifest_path, manifest)) {
        error = "mod.json ausente ou ilegivel";
        return false;
    }

    std::string id;
    std::string name;
    std::string type;
    int schema_version = 0;
    if (!GetJsonInt(manifest, "schema_version", schema_version) || schema_version != 1 ||
        !GetJsonString(manifest, "id", id) || !GetJsonString(manifest, "name", name) ||
        !GetJsonString(manifest, "type", type) || id.empty() || name.empty()) {
        error = "manifesto exige schema_version=1, id, name e type";
        return false;
    }
    if (!IsStableIdentifier(id)) {
        error = "id invalido";
        return false;
    }
    if (id.size() >= sizeof(record.view.id) || name.size() >= sizeof(record.view.name)) {
        error = "id ou name excede a ABI";
        return false;
    }

    record.view.struct_size = sizeof(record.view);
    record.view.state = DmModState::Discovered;
    record.directory_path = directory_path;
    record.directory_utf8 = WideToUtf8(directory_name);
    record.directory_path_utf8 = WideToUtf8(directory_path);
    if (record.directory_utf8.empty() || record.directory_path_utf8.empty()) {
        error = "diretorio nao pode ser convertido para UTF-8";
        return false;
    }
    CopyText(record.view.id, id);
    CopyText(record.view.directory, record.directory_utf8);
    CopyText(record.view.name, name);

    std::string category;
    std::string version;
    std::string author;
    std::string description;
    if (!GetJsonString(manifest, "category", category) || category.empty() ||
        !GetJsonString(manifest, "version", version) || version.empty() ||
        !GetJsonString(manifest, "author", author) || author.empty() ||
        !GetJsonString(manifest, "description", description) || description.empty()) {
        error = "manifesto exige category, version, author e description explicitos";
        return false;
    }
    if (record.directory_utf8.size() >= sizeof(record.view.directory) ||
        category.size() >= sizeof(record.view.category) ||
        version.size() >= sizeof(record.view.version) ||
        author.size() >= sizeof(record.view.author) ||
        description.size() >= sizeof(record.view.description)) {
        error = "metadados do manifesto excedem a ABI";
        return false;
    }
    CopyText(record.view.category, category);
    CopyText(record.view.version, version);
    CopyText(record.view.author, author);
    CopyText(record.view.description, description);

    bool enabled = false;
    if (type == "toggle") {
        record.view.type = DmModType::Toggle;
        if (!ReadEnabledFlag(directory_path, enabled)) {
            error = "toggle exige enabled.txt valido (0 ou 1)";
            return false;
        }
    } else if (type == "action") {
        record.view.type = DmModType::Action;
    } else if (type == "system") {
        record.view.type = DmModType::System;
        if (!ReadEnabledFlag(directory_path, enabled) || !enabled) {
            error = "system exige enabled.txt com valor 1";
            return false;
        }
        bool required = false;
        if (!GetJsonBool(manifest, "required", required)) {
            error = "system exige required booleano";
            return false;
        }
        record.view.required = required ? TRUE : FALSE;
    } else {
        error = "type deve ser toggle, action ou system";
        return false;
    }
    record.view.configured_enabled = enabled ? TRUE : FALSE;

    if (record.view.type == DmModType::Toggle || record.view.type == DmModType::System) {
        std::string plugin;
        if (!GetJsonString(manifest, "plugin", plugin) || !IsPlainFileName(plugin) || !HasExtension(plugin, ".dll")) {
            error = "mod residente exige plugin explicito e sem subdiretorios";
            return false;
        }
        record.plugin_path = directory_path + L"\\" + Utf8ToWide(plugin);
        if (!FileExists(record.plugin_path)) {
            error = "plugin declarado nao existe";
            return false;
        }
    } else {
        std::string executable;
        std::string action_label;
        std::string success_status;
        if (!GetJsonString(manifest, "executable", executable) || !IsPlainFileName(executable) ||
            !HasExtension(executable, ".exe") ||
            !GetJsonString(manifest, "action_label", action_label) || action_label.empty() ||
            action_label.size() >= sizeof(record.view.action_label) ||
            !GetJsonString(manifest, "success_status", success_status) ||
            success_status.empty() || success_status.size() >= sizeof(record.view.status)) {
            error = "action exige executable, action_label e success_status explicitos";
            return false;
        }
        record.executable_path = directory_path + L"\\" + Utf8ToWide(executable);
        if (!FileExists(record.executable_path)) {
            error = "executable declarado nao existe";
            return false;
        }
        CopyText(record.view.action_label, action_label);
        record.action_success_status = success_status;
        bool auto_apply = false;
        if (!GetJsonBool(manifest, "auto_apply", auto_apply)) {
            error = "action exige auto_apply booleano explicito";
            return false;
        }
        record.auto_apply = auto_apply;
    }

    int load_order = 0;
    if (!GetJsonInt(manifest, "load_order", load_order) ||
        load_order < 0 || load_order > 100000) {
        error = "manifesto exige load_order entre 0 e 100000";
        return false;
    }
    record.load_order = load_order;

    if (!ParseOptions(manifest, record, error)) return false;
    if (record.view.type == DmModType::Action && record.view.option_count != 0) {
        error = "action nao aceita options persistentes";
        return false;
    }
    if (record.view.type != DmModType::Action && !ParseConfig(record, error)) return false;
    for (std::uint32_t index = 0; index < record.view.option_count; ++index) {
        record.persisted_values[index] = record.view.options[index].value;
    }
    std::snprintf(record.view.status, sizeof(record.view.status), "Descoberto; aguardando inicializacao.");
    return true;
}

ModRecord* FindRecord(const char* mod_id) {
    if (mod_id == nullptr || mod_id[0] == '\0') return nullptr;
    for (ModRecord& record : g_records) {
        if (std::strcmp(record.view.id, mod_id) == 0) return &record;
    }
    return nullptr;
}

template <typename T>
T ResolveExport(HMODULE module, const char* name) {
    return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(module, name)));
}

std::uint32_t WINAPI ApiGetModCount();
BOOL WINAPI ApiGetMod(std::uint32_t index, DmModView* out_mod);
BOOL WINAPI ApiGetModById(const char* mod_id, DmModView* out_mod);
BOOL WINAPI ApiSetModEnabled(const char* mod_id, BOOL enabled);
BOOL WINAPI ApiSetModOption(const char* mod_id, const char* option_id, const DmModValue* value);
BOOL WINAPI ApiFlushModConfig(const char* mod_id);
BOOL WINAPI ApiExecuteModAction(const char* mod_id);
void WINAPI ApiLog(const char* component, const char* message);
BOOL WINAPI ApiCreateHook(const char* mod_id, void* target, void* detour, void** original);
BOOL WINAPI ApiQueueHook(const char* mod_id, void* target, BOOL enabled);
BOOL WINAPI ApiApplyHooks();
BOOL WINAPI ApiRemoveHook(const char* mod_id, void* target);

const DmModLoaderApi g_api = {
    sizeof(DmModLoaderApi),
    DM_MOD_LOADER_ABI_VERSION,
    &ApiGetModCount,
    &ApiGetMod,
    &ApiGetModById,
    &ApiSetModEnabled,
    &ApiSetModOption,
    &ApiFlushModConfig,
    &ApiExecuteModAction,
    &ApiLog,
    &ApiCreateHook,
    &ApiQueueHook,
    &ApiApplyHooks,
    &ApiRemoveHook,
};

bool LoadAndEnablePlugin(ModRecord& record) {
    if (record.module != nullptr) {
        if (record.view.runtime_enabled != FALSE) return true;
        if (record.enable == nullptr || record.enable() == FALSE) {
            SetRecordStatus(record, DmModState::Failed, "Mod_Enable falhou.");
            return false;
        }
        AcquireSRWLockExclusive(&g_records_lock);
        record.view.runtime_enabled = TRUE;
        record.view.state = DmModState::Enabled;
        std::snprintf(record.view.status, sizeof(record.view.status), "Ativado pela ABI v%u.", DM_MOD_LOADER_ABI_VERSION);
        ReleaseSRWLockExclusive(&g_records_lock);
        return true;
    }

    HMODULE module = LoadLibraryW(record.plugin_path.c_str());
    if (module == nullptr) {
        SetRecordStatus(record, DmModState::Failed, "LoadLibrary falhou (Win32=%lu).", GetLastError());
        return false;
    }

    const auto get_abi = ResolveExport<DmModGetAbiVersionFn>(module, "Mod_GetAbiVersion");
    const auto initialize = ResolveExport<DmModInitializeFn>(module, "Mod_Initialize");
    const auto enable = ResolveExport<DmModEnableFn>(module, "Mod_Enable");
    const auto disable = ResolveExport<DmModDisableFn>(module, "Mod_Disable");
    const auto set_option = ResolveExport<DmModSetOptionFn>(module, "Mod_SetOption");
    const auto shutdown = ResolveExport<DmModShutdownFn>(module, "Mod_Shutdown");
    if (get_abi == nullptr || initialize == nullptr || enable == nullptr || disable == nullptr || shutdown == nullptr ||
        (record.view.option_count > 0 && set_option == nullptr)) {
        record.module = module;
        SetRecordStatus(record, DmModState::Failed, "Plugin nao implementa a ABI nativa obrigatoria.");
        return false;
    }
    if (get_abi() != DM_MOD_LOADER_ABI_VERSION) {
        record.module = module;
        SetRecordStatus(record, DmModState::Failed, "Versao de ABI incompativel.");
        return false;
    }

    const DmModHostContext context = {
        sizeof(DmModHostContext),
        DM_MOD_LOADER_ABI_VERSION,
        &g_api,
        g_game_directory_utf8.c_str(),
        record.directory_path_utf8.c_str(),
        g_game_module_base,
        g_game_module_size,
        g_game_build_verified ? TRUE : FALSE,
    };
    if (initialize(&context) == FALSE) {
        shutdown();
        record.module = module;
        SetRecordStatus(record, DmModState::Failed, "Mod_Initialize falhou.");
        return false;
    }

    for (std::uint32_t index = 0; index < record.view.option_count; ++index) {
        const DmModOptionView& option = record.view.options[index];
        if (set_option(option.id, &option.value) == FALSE) {
            shutdown();
            record.module = module;
            SetRecordStatus(record, DmModState::Failed, "Mod_SetOption falhou para '%s'.", option.id);
            return false;
        }
    }
    if (enable() == FALSE) {
        shutdown();
        record.module = module;
        SetRecordStatus(record, DmModState::Failed, "Mod_Enable falhou.");
        return false;
    }

    AcquireSRWLockExclusive(&g_records_lock);
    record.module = module;
    record.enable = enable;
    record.disable = disable;
    record.set_option = set_option;
    record.shutdown = shutdown;
    record.view.runtime_enabled = TRUE;
    record.view.state = DmModState::Enabled;
    std::snprintf(record.view.status, sizeof(record.view.status), "Carregado e ativado pela ABI v%u.", DM_MOD_LOADER_ABI_VERSION);
    ReleaseSRWLockExclusive(&g_records_lock);
    LogLine(record.view.id, "plugin carregado: %s", WideToUtf8(record.plugin_path).c_str());
    return true;
}

bool DisablePlugin(ModRecord& record) {
    if (record.module == nullptr || record.view.runtime_enabled == FALSE) return true;
    if (record.disable == nullptr || record.disable() == FALSE) {
        SetRecordStatus(record, DmModState::Failed, "Mod_Disable falhou; estado anterior preservado.");
        return false;
    }
    AcquireSRWLockExclusive(&g_records_lock);
    record.view.runtime_enabled = FALSE;
    record.view.state = DmModState::Disabled;
    std::snprintf(record.view.status, sizeof(record.view.status), "Desativado; DLL permanece residente.");
    ReleaseSRWLockExclusive(&g_records_lock);
    return true;
}

bool StartAction(ModRecord& record) {
    if (record.view.type != DmModType::Action || record.executable_path.empty()) return false;
    std::wstring command_line = L"\"" + record.executable_path + L"\"";
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, g_game_directory.c_str(), &startup, &process)) {
        SetRecordStatus(record, DmModState::Failed, "CreateProcess falhou (Win32=%lu).", GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    const DWORD wait_result = WaitForSingleObject(process.hProcess, 30000);
    if (wait_result != WAIT_OBJECT_0) {
        CloseHandle(process.hProcess);
        SetRecordStatus(record, DmModState::Failed, "Action nao concluiu em 30 segundos.");
        return false;
    }
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
        CloseHandle(process.hProcess);
        SetRecordStatus(record, DmModState::Failed, "GetExitCodeProcess falhou (Win32=%lu).", GetLastError());
        return false;
    }
    CloseHandle(process.hProcess);
    if (exit_code != 0) {
        SetRecordStatus(record, DmModState::Failed, "Action terminou com codigo %lu.", exit_code);
        return false;
    }
    SetRecordStatus(record, DmModState::ActionCompleted, "%s",
                    record.action_success_status.c_str());
    LogLine(record.view.id, "action concluida: %s", WideToUtf8(record.executable_path).c_str());
    return true;
}

struct LifecycleGuard {
    LifecycleGuard() { AcquireSRWLockExclusive(&g_lifecycle_lock); }
    ~LifecycleGuard() { ReleaseSRWLockExclusive(&g_lifecycle_lock); }
};

bool FlushRecordConfig(ModRecord& record) {
    if (!record.config_dirty) return true;
    if (PersistConfig(record)) {
        AcquireSRWLockExclusive(&g_records_lock);
        for (std::uint32_t index = 0; index < record.view.option_count; ++index) {
            record.persisted_values[index] = record.view.options[index].value;
        }
        record.config_dirty = false;
        ReleaseSRWLockExclusive(&g_records_lock);
        return true;
    }

    bool rollback_ok = true;
    for (std::uint32_t index = 0; index < record.view.option_count; ++index) {
        if (record.module != nullptr &&
            (record.set_option == nullptr ||
             record.set_option(record.view.options[index].id, &record.persisted_values[index]) == FALSE)) {
            rollback_ok = false;
        }
        AcquireSRWLockExclusive(&g_records_lock);
        record.view.options[index].value = record.persisted_values[index];
        ReleaseSRWLockExclusive(&g_records_lock);
    }
    AcquireSRWLockExclusive(&g_records_lock);
    record.config_dirty = false;
    ReleaseSRWLockExclusive(&g_records_lock);
    if (!rollback_ok && record.view.runtime_enabled != FALSE) DisablePlugin(record);
    SetRecordStatus(record, DmModState::Failed,
                    rollback_ok ? "Falha ao persistir config.json; valores revertidos."
                                : "Falha ao persistir config.json e ao reverter plugin; mod desativado.");
    return false;
}

std::uint32_t WINAPI ApiGetModCount() {
    AcquireSRWLockShared(&g_records_lock);
    const std::uint32_t count = static_cast<std::uint32_t>(g_records.size());
    ReleaseSRWLockShared(&g_records_lock);
    return count;
}

BOOL WINAPI ApiGetMod(std::uint32_t index, DmModView* out_mod) {
    if (out_mod == nullptr || out_mod->struct_size != sizeof(DmModView)) return FALSE;
    AcquireSRWLockShared(&g_records_lock);
    if (index >= g_records.size()) {
        ReleaseSRWLockShared(&g_records_lock);
        return FALSE;
    }
    *out_mod = g_records[index].view;
    ReleaseSRWLockShared(&g_records_lock);
    return TRUE;
}

BOOL WINAPI ApiGetModById(const char* mod_id, DmModView* out_mod) {
    if (out_mod == nullptr || out_mod->struct_size != sizeof(DmModView)) return FALSE;
    AcquireSRWLockShared(&g_records_lock);
    ModRecord* record = FindRecord(mod_id);
    if (record == nullptr) {
        ReleaseSRWLockShared(&g_records_lock);
        return FALSE;
    }
    *out_mod = record->view;
    ReleaseSRWLockShared(&g_records_lock);
    return TRUE;
}

BOOL WINAPI ApiSetModEnabled(const char* mod_id, BOOL enabled) {
    LifecycleGuard guard;
    ModRecord* record = FindRecord(mod_id);
    if (record == nullptr || record->view.type != DmModType::Toggle || record->view.required != FALSE) return FALSE;
    FlushRecordConfig(*record);
    const bool requested = enabled != FALSE;
    const bool previous = record->view.runtime_enabled != FALSE;
    if (requested == previous) {
        if (!PersistEnabledFlag(*record, requested)) {
            SetRecordStatus(*record, DmModState::Failed, "Falha ao persistir enabled.txt.");
            return FALSE;
        }
        AcquireSRWLockExclusive(&g_records_lock);
        record->view.configured_enabled = requested ? TRUE : FALSE;
        ReleaseSRWLockExclusive(&g_records_lock);
        return TRUE;
    }

    const bool lifecycle_ok = requested ? LoadAndEnablePlugin(*record) : DisablePlugin(*record);
    if (!lifecycle_ok) return FALSE;
    if (!PersistEnabledFlag(*record, requested)) {
        const bool rollback_ok = requested ? DisablePlugin(*record) : LoadAndEnablePlugin(*record);
        SetRecordStatus(*record, DmModState::Failed,
                        rollback_ok ? "Falha ao persistir enabled.txt; estado revertido."
                                    : "Falha ao persistir enabled.txt e ao reverter estado.");
        return FALSE;
    }
    AcquireSRWLockExclusive(&g_records_lock);
    record->view.configured_enabled = requested ? TRUE : FALSE;
    ReleaseSRWLockExclusive(&g_records_lock);
    return TRUE;
}

BOOL WINAPI ApiSetModOption(const char* mod_id, const char* option_id, const DmModValue* value) {
    LifecycleGuard guard;
    if (option_id == nullptr || value == nullptr || value->struct_size != sizeof(DmModValue)) return FALSE;
    ModRecord* record = FindRecord(mod_id);
    if (record == nullptr || record->view.type != DmModType::Toggle) return FALSE;
    DmModOptionView* selected = nullptr;
    for (std::uint32_t index = 0; index < record->view.option_count; ++index) {
        if (std::strcmp(record->view.options[index].id, option_id) == 0) {
            selected = &record->view.options[index];
            break;
        }
    }
    if (selected == nullptr || selected->type != value->type) return FALSE;
    if (value->type == DmOptionType::Toggle && value->bool_value != FALSE && value->bool_value != TRUE) return FALSE;
    if (value->type == DmOptionType::SliderInt &&
        (value->int_value < selected->min_int || value->int_value > selected->max_int)) return FALSE;
    if (value->type == DmOptionType::SliderFloat &&
        (!std::isfinite(value->float_value) || value->float_value < selected->min_float ||
         value->float_value > selected->max_float)) return FALSE;

    if (record->module != nullptr &&
        (record->set_option == nullptr || record->set_option(option_id, value) == FALSE)) {
        SetRecordStatus(*record, DmModState::Failed, "Plugin rejeitou a opcao '%s'.", option_id);
        return FALSE;
    }
    AcquireSRWLockExclusive(&g_records_lock);
    selected->value = *value;
    record->config_dirty = true;
    ReleaseSRWLockExclusive(&g_records_lock);
    return TRUE;
}

BOOL WINAPI ApiFlushModConfig(const char* mod_id) {
    LifecycleGuard guard;
    if (mod_id == nullptr || mod_id[0] == '\0') {
        bool all_ok = true;
        for (ModRecord& record : g_records) {
            if (!FlushRecordConfig(record)) all_ok = false;
        }
        return all_ok ? TRUE : FALSE;
    }
    ModRecord* record = FindRecord(mod_id);
    return record != nullptr && FlushRecordConfig(*record) ? TRUE : FALSE;
}

BOOL WINAPI ApiExecuteModAction(const char* mod_id) {
    LifecycleGuard guard;
    ModRecord* record = FindRecord(mod_id);
    return record != nullptr && StartAction(*record) ? TRUE : FALSE;
}

void WINAPI ApiLog(const char* component, const char* message) {
    LogLine(component != nullptr ? component : "plugin", "%s", message != nullptr ? message : "");
}

struct HooksGuard {
    HooksGuard() { AcquireSRWLockExclusive(&g_hooks_lock); }
    ~HooksGuard() { ReleaseSRWLockExclusive(&g_hooks_lock); }
};

const HookEntry* FindHook(void* target) {
    for (const HookEntry& entry : g_hooks) {
        if (entry.target == target) return &entry;
    }
    return nullptr;
}

BOOL WINAPI ApiCreateHook(const char* mod_id, void* target, void* detour, void** original) {
    if (mod_id == nullptr || mod_id[0] == '\0' || target == nullptr || detour == nullptr ||
        original == nullptr) {
        return FALSE;
    }
    HooksGuard guard;
    if (!g_minhook_ready) return FALSE;

    const HookEntry* existing = FindHook(target);
    if (existing != nullptr) {
        LogLine("loader", "hook duplicado rejeitado: %s tentou 0x%llX, ja registrado por %s",
                mod_id, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(target)),
                existing->owner.c_str());
        return FALSE;
    }
    if (MH_CreateHook(target, detour, original) != MH_OK) {
        LogLine("loader", "MH_CreateHook falhou para %s em 0x%llX", mod_id,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(target)));
        return FALSE;
    }
    g_hooks.push_back(HookEntry{target, mod_id});
    return TRUE;
}

BOOL WINAPI ApiQueueHook(const char* mod_id, void* target, BOOL enabled) {
    if (mod_id == nullptr || target == nullptr) return FALSE;
    HooksGuard guard;
    if (!g_minhook_ready) return FALSE;
    const HookEntry* entry = FindHook(target);
    if (entry == nullptr || entry->owner != mod_id) return FALSE;
    const MH_STATUS status =
        enabled != FALSE ? MH_QueueEnableHook(target) : MH_QueueDisableHook(target);
    return status == MH_OK ? TRUE : FALSE;
}

BOOL WINAPI ApiApplyHooks() {
    HooksGuard guard;
    if (!g_minhook_ready) return FALSE;
    return MH_ApplyQueued() == MH_OK ? TRUE : FALSE;
}

BOOL WINAPI ApiRemoveHook(const char* mod_id, void* target) {
    if (mod_id == nullptr || target == nullptr) return FALSE;
    HooksGuard guard;
    if (!g_minhook_ready) return FALSE;
    for (std::size_t index = 0; index < g_hooks.size(); ++index) {
        if (g_hooks[index].target != target) continue;
        if (g_hooks[index].owner != mod_id) return FALSE;
        MH_DisableHook(target);
        const bool removed = MH_RemoveHook(target) == MH_OK;
        g_hooks.erase(g_hooks.begin() + static_cast<std::ptrdiff_t>(index));
        return removed ? TRUE : FALSE;
    }
    return FALSE;
}

bool DiscoverMods() {
    g_discovery_errors = 0;
    const std::wstring mods_directory = g_game_directory + L"\\mods";
    WIN32_FIND_DATAW find_data = {};
    HANDLE find = FindFirstFileW((mods_directory + L"\\*").c_str(), &find_data);
    if (find == INVALID_HANDLE_VALUE) {
        LogLine("loader", "pasta mods ausente (Win32=%lu)", GetLastError());
        return false;
    }

    std::vector<ModRecord> discovered;
    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            std::wcscmp(find_data.cFileName, L".") == 0 || std::wcscmp(find_data.cFileName, L"..") == 0) {
            continue;
        }
        const std::wstring directory_name = find_data.cFileName;
        const std::wstring directory_path = mods_directory + L"\\" + directory_name;
        if (!FileExists(directory_path + L"\\mod.json")) {
            LogLine("loader", "diretorio ignorado sem mod.json: %s", WideToUtf8(directory_name).c_str());
            continue;
        }

        ModRecord record;
        std::string error;
        if (!ParseManifest(directory_path, directory_name, record, error)) {
            LogLine("loader", "manifesto rejeitado em %s: %s", WideToUtf8(directory_name).c_str(), error.c_str());
            ++g_discovery_errors;
            continue;
        }
        const bool duplicate = std::any_of(discovered.begin(), discovered.end(), [&](const ModRecord& existing) {
            return std::strcmp(existing.view.id, record.view.id) == 0;
        });
        if (duplicate) {
            LogLine("loader", "id duplicado rejeitado: %s", record.view.id);
            ++g_discovery_errors;
            continue;
        }
        discovered.push_back(std::move(record));
    } while (FindNextFileW(find, &find_data));
    FindClose(find);

    std::sort(discovered.begin(), discovered.end(), [](const ModRecord& first, const ModRecord& second) {
        if (first.view.type != second.view.type) {
            if (first.view.type == DmModType::System) return true;
            if (second.view.type == DmModType::System) return false;
        }
        if (first.load_order != second.load_order) return first.load_order < second.load_order;
        return std::strcmp(first.view.id, second.view.id) < 0;
    });

    AcquireSRWLockExclusive(&g_records_lock);
    g_records = std::move(discovered);
    ReleaseSRWLockExclusive(&g_records_lock);
    return !g_records.empty();
}

bool IsCommittedRange(const void* address, std::size_t size) {
    if (address == nullptr || size == 0) return false;
    MEMORY_BASIC_INFORMATION information = {};
    if (VirtualQuery(address, &information, sizeof(information)) != sizeof(information) ||
        information.State != MEM_COMMIT ||
        (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    const auto end = reinterpret_cast<std::uintptr_t>(information.BaseAddress) + information.RegionSize;
    return start <= end && size <= end - start;
}

void ResolveGameModule() {
    g_game_module_base = 0;
    g_game_module_size = 0;
    g_game_build_verified = false;

    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (base == 0 || !IsCommittedRange(reinterpret_cast<const void*>(base), sizeof(IMAGE_DOS_HEADER))) {
        LogLine("loader", "modulo do jogo inacessivel; plugins receberao build nao verificada");
        return;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return;

    const auto nt_address = base + static_cast<std::uintptr_t>(dos->e_lfanew);
    if (!IsCommittedRange(reinterpret_cast<const void*>(nt_address), sizeof(IMAGE_NT_HEADERS64))) return;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(nt_address);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return;
    }

    g_game_module_base = base;
    g_game_module_size = nt->OptionalHeader.SizeOfImage;
    g_game_build_verified = nt->FileHeader.TimeDateStamp == kExpectedGameTimestamp &&
                            nt->OptionalHeader.SizeOfImage == kExpectedGameImageSize;
    LogLine("loader", "modulo do jogo em 0x%llX (%lu bytes), build esperada: %s",
            static_cast<unsigned long long>(g_game_module_base),
            static_cast<unsigned long>(g_game_module_size),
            g_game_build_verified ? "sim" : "nao");
}

bool InitializePathsAndLog(const wchar_t* explicit_game_directory) {
    if (explicit_game_directory != nullptr) {
        const DWORD attributes = GetFileAttributesW(explicit_game_directory);
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) return false;
        g_game_directory = explicit_game_directory;
    } else {
        wchar_t executable_path[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, executable_path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) return false;
        wchar_t* separator = std::wcsrchr(executable_path, L'\\');
        if (separator == nullptr) return false;
        *separator = L'\0';
        g_game_directory = executable_path;
    }
    g_game_directory_utf8 = WideToUtf8(g_game_directory);
    if (g_game_directory_utf8.empty()) return false;

    g_log_file = _wfopen((g_game_directory + L"\\mods\\mod_loader.log").c_str(), L"wb");
    if (g_log_file == nullptr) return false;
    LogLine("loader", "Disgaea Mayhem Mod Loader ABI v%u iniciado", DM_MOD_LOADER_ABI_VERSION);
    return true;
}

}  // namespace

DWORD WINAPI DmModLoaderRun(LPVOID) {
    if (!InitializePathsAndLog(nullptr)) return 1;
    ResolveGameModule();
    if (MH_Initialize() != MH_OK) {
        LogLine("loader", "MH_Initialize falhou; nenhum plugin podera instalar hooks");
        return 5;
    }
    g_minhook_ready = true;
    if (!DiscoverMods()) {
        LogLine("loader", "nenhum manifesto valido; loader encerrado em modo fail-closed");
        return 2;
    }

    bool required_system_loaded = false;
    for (ModRecord& record : g_records) {
        if (record.view.type != DmModType::System) continue;
        if (!LoadAndEnablePlugin(record)) {
            LogLine("loader", "system mod falhou: %s", record.view.id);
            if (record.view.required != FALSE) return 3;
        } else if (record.view.required != FALSE) {
            required_system_loaded = true;
        }
    }
    if (!required_system_loaded) {
        LogLine("loader", "nenhum system mod obrigatorio foi carregado");
        return 4;
    }

    for (ModRecord& record : g_records) {
        if (record.view.type == DmModType::Toggle && record.view.configured_enabled != FALSE) {
            AcquireSRWLockExclusive(&g_lifecycle_lock);
            if (!LoadAndEnablePlugin(record)) LogLine("loader", "toggle falhou: %s", record.view.id);
            ReleaseSRWLockExclusive(&g_lifecycle_lock);
        }
    }
    for (ModRecord& record : g_records) {
        if (record.view.type == DmModType::Action && record.auto_apply) {
            AcquireSRWLockExclusive(&g_lifecycle_lock);
            if (!StartAction(record)) LogLine("loader", "auto action falhou: %s", record.view.id);
            ReleaseSRWLockExclusive(&g_lifecycle_lock);
        }
    }
    LogLine("loader", "bootstrap concluido com %zu manifesto(s)", g_records.size());
    return 0;
}

DWORD DmModLoaderValidate(const wchar_t* game_directory) {
    if (game_directory == nullptr || !InitializePathsAndLog(game_directory)) return 10;
    if (!DiscoverMods()) return 11;
    if (g_discovery_errors != 0) return 15;

    bool required_system_found = false;
    for (ModRecord& record : g_records) {
        if (record.view.type == DmModType::Action) continue;
        HMODULE module = LoadLibraryW(record.plugin_path.c_str());
        if (module == nullptr) {
            LogLine("validator", "LoadLibrary falhou para %s (Win32=%lu)", record.view.id, GetLastError());
            return 12;
        }
        const auto get_abi = ResolveExport<DmModGetAbiVersionFn>(module, "Mod_GetAbiVersion");
        const bool valid = get_abi != nullptr && get_abi() == DM_MOD_LOADER_ABI_VERSION &&
            ResolveExport<DmModInitializeFn>(module, "Mod_Initialize") != nullptr &&
            ResolveExport<DmModEnableFn>(module, "Mod_Enable") != nullptr &&
            ResolveExport<DmModDisableFn>(module, "Mod_Disable") != nullptr &&
            ResolveExport<DmModShutdownFn>(module, "Mod_Shutdown") != nullptr &&
            (record.view.option_count == 0 || ResolveExport<DmModSetOptionFn>(module, "Mod_SetOption") != nullptr);
        if (!valid) {
            LogLine("validator", "ABI invalida: %s", record.view.id);
            return 13;
        }
        if (record.view.type == DmModType::System && record.view.required != FALSE) required_system_found = true;
    }
    if (!required_system_found) return 14;
    LogLine("validator", "validacao concluida: %zu manifesto(s), ABI v%u", g_records.size(), DM_MOD_LOADER_ABI_VERSION);
    return 0;
}
