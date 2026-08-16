#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

static bool FileExists(const char* path) {
    const DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool EnsureDirectory(const std::string& path) {
    const DWORD attr = GetFileAttributesA(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0) return true;
    const size_t slash = path.find_last_of("\\/");
    if (slash != std::string::npos) {
        const std::string parent = path.substr(0, slash);
        if (!parent.empty() && parent.back() != ':') EnsureDirectory(parent);
    }
    if (CreateDirectoryA(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

int main() {
    char self[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, self, MAX_PATH)) return 1;
    char* slash = strrchr(self, '\\');
    if (!slash) return 1;
    *slash = '\0';

    const std::string mod_dir = self;
    const std::string source = mod_dir + "\\cheatSetting.dat";
    const std::string game_dir = mod_dir + "\\..\\..";
    const std::string database_dir = game_dir + "\\data\\database";
    const std::string target = database_dir + "\\cheatSetting.dat";

    if (!FileExists(source.c_str())) return 2;
    if (!EnsureDirectory(database_dir)) return 3;
    if (!CopyFileA(source.c_str(), target.c_str(), FALSE)) return 4;
    return 0;
}
