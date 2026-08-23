#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kExpectedDatabaseSize = 68279;
constexpr std::uint32_t kStockMaximum = 500;
constexpr std::uint32_t kExpandedMaximum = 5000;
constexpr std::size_t kMaximumOffsetBeforeNextRecord = 0x30;

struct RecordBoundary {
    const char* id;
    std::uint32_t expected_next_numeric_id;
};

constexpr RecordBoundary kRecords[] = {
    {"CHEAT_SETTING_EXP", 10102},
    {"CHEAT_SETTING_MANA", 10103},
    {"CHEAT_SETTING_HL", 10104},
    {"CHEAT_SETTING_WM", 10105},
    {"CHEAT_SETTING_ITEM_DROPS", 20101},
    {"CHEAT_SETTING_ENEMY_LV", 0},
};

std::wstring DirectoryName(const std::wstring& path) {
    const std::size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return {};
    return path.substr(0, separator);
}

bool IsRegularFile(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool ReadAllBytes(const std::wstring& path, std::vector<std::uint8_t>& output) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        static_cast<unsigned long long>(size.QuadPart) > static_cast<unsigned long long>(SIZE_MAX)) {
        CloseHandle(file);
        return false;
    }
    output.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t total = 0;
    while (total < output.size()) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(output.size() - total, 1U << 20));
        DWORD received = 0;
        if (!ReadFile(file, output.data() + total, request, &received, nullptr) || received == 0) {
            CloseHandle(file);
            return false;
        }
        total += received;
    }
    CloseHandle(file);
    return true;
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void WriteU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

bool FindUniqueRecord(const std::vector<std::uint8_t>& bytes, const char* id, std::size_t& offset) {
    const auto* first = reinterpret_cast<const std::uint8_t*>(id);
    const std::size_t length = std::strlen(id) + 1;
    const auto match = std::search(bytes.begin(), bytes.end(), first, first + length);
    if (match == bytes.end()) return false;
    const auto duplicate = std::search(match + static_cast<std::ptrdiff_t>(length), bytes.end(), first, first + length);
    if (duplicate != bytes.end()) return false;
    offset = static_cast<std::size_t>(match - bytes.begin());
    return true;
}

bool PatchValidatedDatabase(std::vector<std::uint8_t>& bytes) {
    if (bytes.size() != kExpectedDatabaseSize) return false;

    std::size_t positions[sizeof(kRecords) / sizeof(kRecords[0])] = {};
    for (std::size_t index = 0; index < sizeof(kRecords) / sizeof(kRecords[0]); ++index) {
        if (!FindUniqueRecord(bytes, kRecords[index].id, positions[index])) return false;
        if (index > 0 && positions[index] <= positions[index - 1]) return false;
    }

    for (std::size_t index = 0; index + 1 < sizeof(kRecords) / sizeof(kRecords[0]); ++index) {
        if (positions[index + 1] < kMaximumOffsetBeforeNextRecord) return false;
        const std::size_t maximum_offset = positions[index + 1] - kMaximumOffsetBeforeNextRecord;
        if (maximum_offset < 12 || maximum_offset + 36 > bytes.size()) return false;

        if (ReadU32(bytes, maximum_offset - 12) != 1 ||
            ReadU32(bytes, maximum_offset - 8) != 100 ||
            ReadU32(bytes, maximum_offset - 4) != 90 ||
            ReadU32(bytes, maximum_offset + 32) != kRecords[index].expected_next_numeric_id) {
            return false;
        }
        for (std::size_t zero_offset = 4; zero_offset <= 24; zero_offset += 4) {
            if (ReadU32(bytes, maximum_offset + zero_offset) != 0) return false;
        }

        const std::uint32_t current_maximum = ReadU32(bytes, maximum_offset);
        if (current_maximum != kStockMaximum && current_maximum != kExpandedMaximum) return false;
        WriteU32(bytes, maximum_offset, kExpandedMaximum);
    }
    return true;
}

bool WriteAtomically(const std::wstring& target, const std::vector<std::uint8_t>& bytes) {
    const std::wstring temporary = target + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    std::size_t total = 0;
    bool success = true;
    while (total < bytes.size()) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - total, 1U << 20));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + total, request, &written, nullptr) || written != request) {
            success = false;
            break;
        }
        total += written;
    }
    if (success && !FlushFileBuffers(file)) success = false;
    CloseHandle(file);
    if (!success) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

}  // namespace

int wmain() {
    wchar_t executable_path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, executable_path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return 1;

    const std::wstring mod_directory = DirectoryName(executable_path);
    const std::wstring mods_directory = DirectoryName(mod_directory);
    const std::wstring game_directory = DirectoryName(mods_directory);
    if (mod_directory.empty() || mods_directory.empty() || game_directory.empty() ||
        !IsRegularFile(game_directory + L"\\Disgaea_Mayhem.exe")) {
        return 2;
    }

    const std::wstring target = game_directory + L"\\data\\database\\cheatSetting.dat";
    std::vector<std::uint8_t> bytes;
    if (!ReadAllBytes(target, bytes)) return 3;
    if (!PatchValidatedDatabase(bytes)) return 4;
    if (!WriteAtomically(target, bytes)) return 5;

    std::vector<std::uint8_t> verification;
    if (!ReadAllBytes(target, verification) || verification != bytes) return 6;
    return 0;
}
