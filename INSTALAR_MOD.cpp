#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace {

struct InstallEntry {
    const wchar_t* relative_path;
    bool preserve_existing;
};

constexpr InstallEntry kInstallEntries[] = {
    {L"dxgi.dll", false},
    {L"SmokeAPI.config.json", false},
    {L"tools\\mod_loader_validate.exe", false},
    {L"tools\\INSTRUCOES_RESGATES.txt", false},

    {L"mods\\mod_menu\\mod.json", false},
    {L"mods\\mod_menu\\config.json", true},
    {L"mods\\mod_menu\\enabled.txt", true},
    {L"mods\\mod_menu\\mod_menu.dll", false},
    {L"mods\\mod_menu\\README.md", false},
    {L"mods\\mod_menu\\main_menu\\mods_slot.dds", false},
    {L"mods\\mod_menu\\main_menu\\OFL.txt", false},

    {L"mods\\chara_world\\mod.json", false},
    {L"mods\\chara_world\\config.json", true},
    {L"mods\\chara_world\\enabled.txt", true},
    {L"mods\\chara_world\\chara_world.dll", false},
    {L"mods\\chara_world\\README.md", false},

    {L"mods\\item_world\\mod.json", false},
    {L"mods\\item_world\\config.json", true},
    {L"mods\\item_world\\enabled.txt", true},
    {L"mods\\item_world\\item_world.dll", false},
    {L"mods\\item_world\\README.md", false},

    {L"mods\\cheat_shop\\mod.json", false},
    {L"mods\\cheat_shop\\config.json", true},
    {L"mods\\cheat_shop\\enabled.txt", true},
    {L"mods\\cheat_shop\\cheat_shop.dll", false},
    {L"mods\\cheat_shop\\README.md", false},

    {L"mods\\dark_assembly\\mod.json", false},
    {L"mods\\dark_assembly\\config.json", true},
    {L"mods\\dark_assembly\\enabled.txt", true},
    {L"mods\\dark_assembly\\dark_assembly.dll", false},
    {L"mods\\dark_assembly\\README.md", false},

    {L"mods\\dlc_unlocker\\mod.json", false},
    {L"mods\\dlc_unlocker\\config.json", true},
    {L"mods\\dlc_unlocker\\enabled.txt", true},
    {L"mods\\dlc_unlocker\\dlc_unlocker.dll", false},
    {L"mods\\dlc_unlocker\\README.md", false},

    {L"mods\\tactical_ai\\mod.json", false},
    {L"mods\\tactical_ai\\config.json", true},
    {L"mods\\tactical_ai\\enabled.txt", true},
    {L"mods\\tactical_ai\\tactical_ai.dll", false},
    {L"mods\\tactical_ai\\README.md", false},

    {L"mods\\safe_backup\\mod.json", false},
    {L"mods\\safe_backup\\config.json", true},
    {L"mods\\safe_backup\\enabled.txt", true},
    {L"mods\\safe_backup\\safe_backup.dll", false},
    {L"mods\\safe_backup\\README.md", false},
};

constexpr const wchar_t* kObsoleteFiles[] = {
    L"mods\\registry.json",
    L"mods\\native\\DisgaeaMayhemModMenu.dll",
    L"mods\\main_menu\\OFL.txt",
    L"mods\\main_menu\\mods_slot.dds",
};

struct TransactionRecord {
    std::wstring target;
    std::wstring backup;
    bool had_original = false;
};

bool IsFile(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool IsDirectory(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring JoinPath(const std::wstring& base, const std::wstring& child) {
    if (base.empty()) return child;
    if (base.back() == L'\\' || base.back() == L'/') return base + child;
    return base + L"\\" + child;
}

std::wstring ParentPath(const std::wstring& path) {
    const std::size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring() : path.substr(0, separator);
}

bool CanonicalizePath(const std::wstring& input, std::wstring& output) {
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetFullPathNameW(input.c_str(),
                                          static_cast<DWORD>(buffer.size()),
                                          buffer.data(), nullptr);
    if (length == 0 || length >= buffer.size()) return false;
    output.assign(buffer.data(), length);
    while (output.size() > 3 &&
           (output.back() == L'\\' || output.back() == L'/')) {
        output.pop_back();
    }
    return true;
}

bool SamePath(const std::wstring& first, const std::wstring& second) {
    std::wstring canonical_first;
    std::wstring canonical_second;
    return CanonicalizePath(first, canonical_first) &&
           CanonicalizePath(second, canonical_second) &&
           _wcsicmp(canonical_first.c_str(), canonical_second.c_str()) == 0;
}

bool EnsureDirectory(const std::wstring& path,
                     std::vector<std::wstring>* created_directories = nullptr) {
    if (path.empty() || IsDirectory(path)) return true;
    const std::wstring parent = ParentPath(path);
    if (!parent.empty() && parent != path &&
        !EnsureDirectory(parent, created_directories)) {
        return false;
    }
    if (CreateDirectoryW(path.c_str(), nullptr) != FALSE) {
        if (created_directories != nullptr) created_directories->push_back(path);
        return true;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS && IsDirectory(path);
}

bool RemoveTree(const std::wstring& path) {
    if (!IsDirectory(path)) return !IsFile(path) || DeleteFileW(path.c_str()) != FALSE;
    const std::wstring pattern = JoinPath(path, L"*");
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (std::wcscmp(data.cFileName, L".") == 0 ||
                std::wcscmp(data.cFileName, L"..") == 0) {
                continue;
            }
            const std::wstring child = JoinPath(path, data.cFileName);
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                if (!RemoveTree(child)) {
                    FindClose(find);
                    return false;
                }
            } else {
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (DeleteFileW(child.c_str()) == FALSE) {
                    FindClose(find);
                    return false;
                }
            }
        } while (FindNextFileW(find, &data) != FALSE);
        const DWORD error = GetLastError();
        FindClose(find);
        if (error != ERROR_NO_MORE_FILES) return false;
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
        return false;
    }
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryW(path.c_str()) != FALSE;
}

bool GetInstallerDirectory(std::wstring& output) {
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return false;
    return CanonicalizePath(ParentPath(std::wstring(buffer.data(), length)), output);
}

bool GameIsRunning() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return true;
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    bool running = false;
    if (Process32FirstW(snapshot, &entry) != FALSE) {
        do {
            if (_wcsicmp(entry.szExeFile, L"Disgaea_Mayhem.exe") == 0) {
                running = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry) != FALSE);
    }
    CloseHandle(snapshot);
    return running;
}

void AddGameCandidate(const std::wstring& path,
                      std::vector<std::wstring>& candidates) {
    std::wstring canonical;
    if (!CanonicalizePath(path, canonical) ||
        !IsFile(JoinPath(canonical, L"Disgaea_Mayhem.exe"))) {
        return;
    }
    const auto duplicate = std::find_if(
        candidates.begin(), candidates.end(),
        [&canonical](const std::wstring& existing) {
            return _wcsicmp(existing.c_str(), canonical.c_str()) == 0;
        });
    if (duplicate == candidates.end()) candidates.push_back(canonical);
}

bool DiscoverGameDirectory(const std::wstring& installer_directory,
                           std::wstring& output) {
    if (IsFile(JoinPath(installer_directory, L"Disgaea_Mayhem.exe"))) {
        output = installer_directory;
        return true;
    }

    const std::wstring parent = ParentPath(installer_directory);
    if (!parent.empty() &&
        IsFile(JoinPath(parent, L"Disgaea_Mayhem.exe"))) {
        output = parent;
        return true;
    }

    std::vector<std::wstring> candidates;
    const DWORD drives = GetLogicalDrives();
    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        const DWORD bit = 1UL << (letter - L'A');
        if ((drives & bit) == 0) continue;
        wchar_t root[] = {letter, L':', L'\\', L'\0'};
        if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
        const std::wstring drive(root);
        AddGameCandidate(JoinPath(drive,
            L"Steam\\steamapps\\common\\Disgaea Mayhem"), candidates);
        AddGameCandidate(JoinPath(drive,
            L"SteamLibrary\\steamapps\\common\\Disgaea Mayhem"), candidates);
        AddGameCandidate(JoinPath(drive,
            L"Program Files (x86)\\Steam\\steamapps\\common\\Disgaea Mayhem"),
            candidates);
        AddGameCandidate(JoinPath(drive,
            L"Program Files\\Steam\\steamapps\\common\\Disgaea Mayhem"),
            candidates);
    }

    if (candidates.size() == 1) {
        output = candidates.front();
        return true;
    }
    if (candidates.size() > 1) {
        std::wprintf(L"[ERRO] Mais de uma instalacao foi encontrada:\n");
        for (const std::wstring& candidate : candidates) {
            std::wprintf(L"  %ls\n", candidate.c_str());
        }
        std::wprintf(L"Informe a pasta correta como argumento do instalador.\n");
    }
    return false;
}

bool FilesEqual(const std::wstring& first, const std::wstring& second) {
    HANDLE first_file = CreateFileW(first.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (first_file == INVALID_HANDLE_VALUE) return false;
    HANDLE second_file = CreateFileW(second.c_str(), GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (second_file == INVALID_HANDLE_VALUE) {
        CloseHandle(first_file);
        return false;
    }

    LARGE_INTEGER first_size = {};
    LARGE_INTEGER second_size = {};
    bool equal = GetFileSizeEx(first_file, &first_size) != FALSE &&
                 GetFileSizeEx(second_file, &second_size) != FALSE &&
                 first_size.QuadPart == second_size.QuadPart;
    std::uint8_t first_buffer[64 * 1024] = {};
    std::uint8_t second_buffer[64 * 1024] = {};
    while (equal) {
        DWORD first_read = 0;
        DWORD second_read = 0;
        if (ReadFile(first_file, first_buffer, sizeof(first_buffer), &first_read, nullptr) == FALSE ||
            ReadFile(second_file, second_buffer, sizeof(second_buffer), &second_read, nullptr) == FALSE ||
            first_read != second_read) {
            equal = false;
            break;
        }
        if (first_read == 0) break;
        if (!std::equal(first_buffer, first_buffer + first_read, second_buffer)) {
            equal = false;
            break;
        }
    }
    CloseHandle(second_file);
    CloseHandle(first_file);
    return equal;
}

bool ValidatePackage(const std::wstring& package_root) {
    for (const InstallEntry& entry : kInstallEntries) {
        const std::wstring source = JoinPath(package_root, entry.relative_path);
        if (!IsFile(source)) {
            std::wprintf(L"[ERRO] Pacote incompleto: %ls\n", entry.relative_path);
            return false;
        }
    }
    const std::wstring smoke = JoinPath(package_root,
                                        L"SmokeAPI\\smoke_api64.dll");
    if (!IsFile(smoke)) {
        std::wprintf(L"[ERRO] Pacote incompleto: SmokeAPI\\smoke_api64.dll\n");
        return false;
    }
    return true;
}

bool CopyFileTransactional(const std::wstring& source,
                           const std::wstring& target,
                           const std::wstring& relative_target,
                           const std::wstring& transaction_root,
                           std::vector<TransactionRecord>& records,
                           std::vector<std::wstring>& created_directories) {
    if (SamePath(source, target)) return true;
    if (!IsFile(source)) {
        std::wprintf(L"[ERRO] Origem ausente: %ls\n", source.c_str());
        return false;
    }
    if (!EnsureDirectory(ParentPath(target), &created_directories)) {
        std::wprintf(L"[ERRO] Nao foi possivel criar a pasta de destino de %ls.\n",
                     target.c_str());
        return false;
    }

    TransactionRecord record;
    record.target = target;
    record.backup = JoinPath(JoinPath(transaction_root, L"backup"), relative_target);
    record.had_original = IsFile(target);
    if (record.had_original) {
        if (!EnsureDirectory(ParentPath(record.backup)) ||
            CopyFileW(target.c_str(), record.backup.c_str(), FALSE) == FALSE) {
            std::wprintf(L"[ERRO] Falha ao guardar o estado anterior de %ls (Win32=%lu).\n",
                         relative_target.c_str(), GetLastError());
            return false;
        }
    }
    records.push_back(record);

    const std::wstring temporary = target + L".installing";
    if (IsFile(temporary) && DeleteFileW(temporary.c_str()) == FALSE) {
        std::wprintf(L"[ERRO] Nao foi possivel limpar %ls (Win32=%lu).\n",
                     temporary.c_str(), GetLastError());
        return false;
    }
    if (CopyFileW(source.c_str(), temporary.c_str(), FALSE) == FALSE ||
        MoveFileExW(temporary.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        const DWORD error = GetLastError();
        DeleteFileW(temporary.c_str());
        std::wprintf(L"[ERRO] Falha ao instalar %ls (Win32=%lu).\n",
                     relative_target.c_str(), error);
        return false;
    }
    std::wprintf(L"[OK] %ls\n", relative_target.c_str());
    return true;
}

bool RemoveFileTransactional(const std::wstring& target,
                             const std::wstring& relative_target,
                             const std::wstring& transaction_root,
                             std::vector<TransactionRecord>& records) {
    if (!IsFile(target)) return true;
    TransactionRecord record;
    record.target = target;
    record.backup = JoinPath(JoinPath(transaction_root, L"backup"), relative_target);
    record.had_original = true;
    if (!EnsureDirectory(ParentPath(record.backup)) ||
        CopyFileW(target.c_str(), record.backup.c_str(), FALSE) == FALSE) {
        std::wprintf(L"[ERRO] Falha ao guardar artefato obsoleto %ls (Win32=%lu).\n",
                     relative_target.c_str(), GetLastError());
        return false;
    }
    records.push_back(record);
    if (DeleteFileW(target.c_str()) == FALSE) {
        std::wprintf(L"[ERRO] Falha ao remover artefato obsoleto %ls (Win32=%lu).\n",
                     relative_target.c_str(), GetLastError());
        return false;
    }
    std::wprintf(L"[OK] Removido: %ls\n", relative_target.c_str());
    return true;
}

bool Rollback(const std::vector<TransactionRecord>& records,
              const std::vector<std::wstring>& created_directories) {
    bool ok = true;
    for (auto record = records.rbegin(); record != records.rend(); ++record) {
        const std::wstring temporary = record->target + L".rollback";
        DeleteFileW(temporary.c_str());
        if (record->had_original) {
            if (CopyFileW(record->backup.c_str(), temporary.c_str(), FALSE) == FALSE ||
                MoveFileExW(temporary.c_str(), record->target.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
                DeleteFileW(temporary.c_str());
                ok = false;
            }
        } else if (IsFile(record->target) && DeleteFileW(record->target.c_str()) == FALSE) {
            ok = false;
        }
    }
    for (auto directory = created_directories.rbegin();
         directory != created_directories.rend(); ++directory) {
        RemoveDirectoryW(directory->c_str());
    }
    return ok;
}

bool InstallSmokeApi(const std::wstring& package_root,
                     const std::wstring& game_root,
                     const std::wstring& transaction_root,
                     std::vector<TransactionRecord>& records,
                     std::vector<std::wstring>& created_directories) {
    const std::wstring wrapper = JoinPath(package_root,
        L"SmokeAPI\\smoke_api64.dll");
    const std::wstring current = JoinPath(game_root, L"steam_api64.dll");
    const std::wstring original = JoinPath(game_root, L"steam_api64_o.dll");

    if (!IsFile(original)) {
        if (!IsFile(current)) {
            std::wprintf(L"[ERRO] steam_api64.dll original ausente.\n");
            return false;
        }
        if (FilesEqual(current, wrapper)) {
            std::wprintf(L"[ERRO] SmokeAPI ja esta em steam_api64.dll, mas steam_api64_o.dll esta ausente.\n");
            return false;
        }
        if (!CopyFileTransactional(current, original, L"steam_api64_o.dll",
                                   transaction_root, records,
                                   created_directories)) {
            return false;
        }
    } else if (FilesEqual(original, wrapper)) {
        std::wprintf(L"[ERRO] steam_api64_o.dll nao contem a biblioteca original da Steam.\n");
        return false;
    }

    return CopyFileTransactional(wrapper, current, L"steam_api64.dll",
                                 transaction_root, records,
                                 created_directories);
}

bool RunValidator(const std::wstring& game_root) {
    const std::wstring executable = JoinPath(game_root,
        L"tools\\mod_loader_validate.exe");
    std::wstring command = L"\"" + executable + L"\" \"" + game_root + L"\"";
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (CreateProcessW(executable.c_str(), mutable_command.data(), nullptr, nullptr,
                       FALSE, 0, nullptr, game_root.c_str(), &startup, &process) == FALSE) {
        std::wprintf(L"[ERRO] Falha ao iniciar o validador (Win32=%lu).\n",
                     GetLastError());
        return false;
    }
    const DWORD wait = WaitForSingleObject(process.hProcess, 60000);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 125);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        std::wprintf(L"[ERRO] O validador nao terminou em 60 segundos.\n");
        return false;
    }
    DWORD exit_code = 1;
    const BOOL read_exit = GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (read_exit == FALSE || exit_code != 0) {
        std::wprintf(L"[ERRO] Validacao ABI falhou (codigo=%lu).\n", exit_code);
        return false;
    }
    return true;
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
    SetConsoleOutputCP(CP_UTF8);
    std::wprintf(L"Disgaea Mayhem - Instalador do Mod Loader\n\n");

    if (argument_count > 2) {
        std::wprintf(L"Uso: INSTALAR_MOD.exe [pasta-do-jogo]\n");
        return 64;
    }
    if (GameIsRunning()) {
        std::wprintf(L"[ERRO] Feche Disgaea Mayhem antes da instalacao.\n");
        return 2;
    }

    std::wstring package_root;
    if (!GetInstallerDirectory(package_root) || !ValidatePackage(package_root)) {
        return 3;
    }

    std::wstring game_root;
    if (argument_count == 2) {
        if (!CanonicalizePath(arguments[1], game_root) ||
            !IsFile(JoinPath(game_root, L"Disgaea_Mayhem.exe"))) {
            std::wprintf(L"[ERRO] A pasta informada nao contem Disgaea_Mayhem.exe.\n");
            return 4;
        }
    } else if (!DiscoverGameDirectory(package_root, game_root)) {
        std::wprintf(L"[ERRO] Instalacao nao encontrada. Execute novamente informando a pasta do jogo.\n");
        return 5;
    }

    std::wprintf(L"[OK] Jogo: %ls\n", game_root.c_str());
    const std::wstring transaction_root = JoinPath(
        game_root, L".dm_mod_install_transaction_" +
                   std::to_wstring(GetCurrentProcessId()));
    if (IsDirectory(transaction_root) || IsFile(transaction_root) ||
        !EnsureDirectory(transaction_root)) {
        std::wprintf(L"[ERRO] Nao foi possivel criar a transacao de instalacao.\n");
        return 6;
    }

    std::vector<TransactionRecord> records;
    std::vector<std::wstring> created_directories;
    bool installed = InstallSmokeApi(package_root, game_root, transaction_root,
                                     records, created_directories);
    for (const InstallEntry& entry : kInstallEntries) {
        if (!installed) break;
        const std::wstring source = JoinPath(package_root, entry.relative_path);
        const std::wstring target = JoinPath(game_root, entry.relative_path);
        if (entry.preserve_existing && IsFile(target)) {
            std::wprintf(L"[OK] Preservado: %ls\n", entry.relative_path);
            continue;
        }
        installed = CopyFileTransactional(source, target, entry.relative_path,
                                          transaction_root, records,
                                          created_directories);
    }
    for (const wchar_t* relative : kObsoleteFiles) {
        if (!installed) break;
        installed = RemoveFileTransactional(JoinPath(game_root, relative), relative,
                                            transaction_root, records);
    }
    if (installed) installed = RunValidator(game_root);

    if (!installed) {
        const bool rollback_ok = Rollback(records, created_directories);
        const bool cleanup_ok = RemoveTree(transaction_root);
        std::wprintf(rollback_ok && cleanup_ok
                         ? L"[ERRO] Instalacao cancelada; estado anterior restaurado.\n"
                         : L"[ERRO] Instalacao cancelada e o rollback nao foi concluido.\n");
        return rollback_ok && cleanup_ok ? 7 : 8;
    }

    RemoveDirectoryW(JoinPath(game_root, L"mods\\native").c_str());
    RemoveDirectoryW(JoinPath(game_root, L"mods\\main_menu").c_str());
    if (!RemoveTree(transaction_root)) {
        std::wprintf(L"[ERRO] Instalacao validada, mas a transacao temporaria nao foi removida.\n");
        return 9;
    }

    std::wprintf(L"\n[OK] Loader, Mod Menu, oito mods e SmokeAPI instalados e validados.\n");
    return 0;
}
