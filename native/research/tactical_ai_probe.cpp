#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr std::uintptr_t kTacticsManagementVtableRva = 0x00A1BEE0;
constexpr std::uintptr_t kTacticsVtableRva = 0x00A1BEF0;
constexpr std::uintptr_t kControllerVtableRva = 0x00A1BF00;
constexpr std::uintptr_t kEnemyStatusVtableRva = 0x00A1BF10;
constexpr std::uintptr_t kTacticalStateVtableRva = 0x00A1C030;
constexpr std::uintptr_t kExploreUnitVtableRva = 0x00A1D088;

struct Kind {
    const char* name;
    std::uintptr_t rva;
};

constexpr std::array<Kind, 18> kKinds = {{
    {"tactics_management", kTacticsManagementVtableRva},
    {"tactics", kTacticsVtableRva},
    {"controller", kControllerVtableRva},
    {"enemy_status", kEnemyStatusVtableRva},
    {"tactical_state", kTacticalStateVtableRva},
    {"state_free", 0x00A1C9F0},
    {"state_no_allocate", 0x00A1C938},
    {"state_wait_after_damage", 0x00A1C880},
    {"state_wait", 0x00A1C7C8},
    {"state_wait_motion", 0x00A1C710},
    {"state_attack_wait", 0x00A1C540},
    {"state_attack", 0x00A1C480},
    {"state_attack_set", 0x00A1C3C0},
    {"state_attack_rush", 0x00A1C300},
    {"state_attack_counter", 0x00A1C240},
    {"state_searching", 0x00A1C188},
    {"state_move", 0x00A1C0C0},
    {"explore_unit", kExploreUnitVtableRva},
}};

struct Hit {
    std::uintptr_t address = 0;
    std::size_t kind = 0;
};

std::uintptr_t g_module_base = 0;

DWORD FindGameProcess() {
    const HANDLE snapshot =
        CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"Disgaea_Mayhem.exe") == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
}

bool IsReadableProtection(DWORD protection) {
    if ((protection & PAGE_GUARD) != 0 ||
        (protection & PAGE_NOACCESS) != 0) {
        return false;
    }
    const DWORD base = protection & 0xFF;
    return base == PAGE_READONLY || base == PAGE_READWRITE ||
           base == PAGE_WRITECOPY || base == PAGE_EXECUTE_READ ||
           base == PAGE_EXECUTE_READWRITE ||
           base == PAGE_EXECUTE_WRITECOPY;
}

template <typename T>
bool ReadRemote(HANDLE process, std::uintptr_t address, T& value) {
    SIZE_T bytes_read = 0;
    return ReadProcessMemory(process, reinterpret_cast<const void*>(address),
                             &value, sizeof(value), &bytes_read) != FALSE &&
           bytes_read == sizeof(value);
}

std::uintptr_t ReadVtable(HANDLE process, std::uintptr_t address) {
    std::uintptr_t value = 0;
    ReadRemote(process, address, value);
    return value;
}

void PrintRttiName(HANDLE process, std::uintptr_t vtable) {
    if (vtable < sizeof(std::uintptr_t) || g_module_base == 0) return;
    std::uintptr_t locator = 0;
    if (!ReadRemote(process, vtable - sizeof(std::uintptr_t), locator)) return;
    std::array<std::uint32_t, 6> col{};
    SIZE_T bytes_read = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<const void*>(locator),
                           col.data(), sizeof(col), &bytes_read) ||
        bytes_read != sizeof(col) || col[0] != 1) {
        return;
    }
    std::array<char, 192> name{};
    if (!ReadProcessMemory(process,
                           reinterpret_cast<const void*>(
                               g_module_base + col[3] + 0x10),
                           name.data(), name.size() - 1, &bytes_read) ||
        bytes_read == 0) {
        return;
    }
    name.back() = '\0';
    std::printf(",td=0x%X,type=%s", col[3], name.data());
}

std::vector<Hit> ScanVtables(HANDLE process, std::uintptr_t module_base) {
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    std::uintptr_t cursor = reinterpret_cast<std::uintptr_t>(
        system_info.lpMinimumApplicationAddress);
    const std::uintptr_t maximum = reinterpret_cast<std::uintptr_t>(
        system_info.lpMaximumApplicationAddress);
    constexpr SIZE_T kChunkSize = 1024 * 1024;
    std::vector<std::uint8_t> buffer(kChunkSize);
    std::vector<Hit> hits;

    while (cursor < maximum) {
        MEMORY_BASIC_INFORMATION information{};
        if (VirtualQueryEx(process, reinterpret_cast<const void*>(cursor),
                           &information, sizeof(information)) == 0) {
            break;
        }
        const std::uintptr_t region =
            reinterpret_cast<std::uintptr_t>(information.BaseAddress);
        const std::uintptr_t region_end = region + information.RegionSize;
        if (information.State == MEM_COMMIT &&
            IsReadableProtection(information.Protect)) {
            for (std::uintptr_t chunk = region; chunk < region_end;) {
                const SIZE_T wanted = static_cast<SIZE_T>(
                    std::min<std::uintptr_t>(kChunkSize, region_end - chunk));
                SIZE_T bytes_read = 0;
                if (ReadProcessMemory(process,
                                      reinterpret_cast<const void*>(chunk),
                                      buffer.data(), wanted, &bytes_read)) {
                    for (SIZE_T offset = 0;
                         offset + sizeof(std::uintptr_t) <= bytes_read;
                         offset += alignof(std::uintptr_t)) {
                        std::uintptr_t value = 0;
                        std::memcpy(&value, buffer.data() + offset,
                                    sizeof(value));
                        for (std::size_t kind = 0; kind < kKinds.size();
                             ++kind) {
                            if (value == module_base + kKinds[kind].rva) {
                                hits.push_back({chunk + offset, kind});
                                break;
                            }
                        }
                    }
                }
                chunk += wanted;
            }
        }
        if (region_end <= cursor) break;
        cursor = region_end;
    }
    return hits;
}

void PrintPointer(HANDLE process, const char* name, std::uintptr_t address) {
    std::uintptr_t pointer = 0;
    if (!ReadRemote(process, address, pointer)) {
        std::printf(" %s=<ilegivel>", name);
        return;
    }
    const std::uintptr_t vtable = ReadVtable(process, pointer);
    std::printf(" %s=0x%llX(vt=0x%llX)", name,
                static_cast<unsigned long long>(pointer),
                static_cast<unsigned long long>(vtable));
    PrintRttiName(process, vtable);
}

void PrintTacticsSubobject(HANDLE process, std::uintptr_t address) {
    std::uintptr_t object = 0;
    if (!ReadRemote(process, address, object)) return;
    for (std::size_t offset = 0; offset <= 0x80;
         offset += sizeof(std::uintptr_t)) {
        std::uintptr_t value = 0;
        if (ReadRemote(process, object + offset, value) &&
            value == g_module_base + kEnemyStatusVtableRva) {
            std::printf(" status-subobject=+0x%zX", offset);
            return;
        }
    }
}

void PrintTacticsLinks(HANDLE process, std::uintptr_t address) {
    std::uintptr_t object = 0;
    if (!ReadRemote(process, address, object)) return;
    for (std::size_t offset = sizeof(std::uintptr_t); offset <= 0x200;
         offset += sizeof(std::uintptr_t)) {
        std::uintptr_t pointer = 0;
        if (!ReadRemote(process, object + offset, pointer) || pointer == 0) {
            continue;
        }
        const std::uintptr_t vtable = ReadVtable(process, pointer);
        const char* kind = nullptr;
        if (vtable == g_module_base + kControllerVtableRva) {
            kind = "controller";
        } else if (vtable == g_module_base + kEnemyStatusVtableRva) {
            kind = "status";
        } else if (vtable == g_module_base + kExploreUnitVtableRva) {
            kind = "unit";
        } else if (vtable == g_module_base + kTacticsManagementVtableRva) {
            kind = "management";
        }
        if (kind != nullptr) {
            std::printf(" link+0x%zX=%s(0x%llX)", offset, kind,
                        static_cast<unsigned long long>(pointer));
        }
    }
}

void PrintMovementValues(HANDLE process, std::uintptr_t state) {
    float lateral = 0.0F;
    float base = 0.0F;
    float run = 0.0F;
    float dive = 0.0F;
    if (ReadRemote(process, state + 0x90, lateral) &&
        ReadRemote(process, state + 0x94, base) &&
        ReadRemote(process, state + 0x98, run) &&
        ReadRemote(process, state + 0x9C, dive)) {
        std::printf(" move[side=%.3f base=%.3f run=%.3f dive=%.3f]",
                    lateral, base, run, dive);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const DWORD pid = FindGameProcess();
    if (pid == 0) {
        std::fprintf(stderr, "Disgaea_Mayhem.exe nao esta em execucao.\n");
        return 2;
    }
    const HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION |
                                           PROCESS_VM_READ,
                                       FALSE, pid);
    if (process == nullptr) {
        std::fprintf(stderr, "OpenProcess falhou: %lu\n", GetLastError());
        return 3;
    }

    HMODULE module = nullptr;
    DWORD needed = 0;
    MODULEINFO module_info{};
    if (!EnumProcessModules(process, &module, sizeof(module), &needed) ||
        module == nullptr ||
        !GetModuleInformation(process, module, &module_info,
                              sizeof(module_info))) {
        std::fprintf(stderr, "Modulo principal indisponivel: %lu\n",
                     GetLastError());
        CloseHandle(process);
        return 4;
    }
    const auto module_base =
        reinterpret_cast<std::uintptr_t>(module_info.lpBaseOfDll);
    g_module_base = module_base;
    std::printf("pid=%lu base=0x%llX size=0x%lX\n", pid,
                static_cast<unsigned long long>(module_base),
                module_info.SizeOfImage);
    if (argc == 2 && std::strcmp(argv[1], "--types") == 0) {
        for (const Kind& kind : kKinds) {
            std::printf("%s(vt=0x%llX", kind.name,
                        static_cast<unsigned long long>(module_base + kind.rva));
            PrintRttiName(process, module_base + kind.rva);
            std::puts(")");
        }
        CloseHandle(process);
        return 0;
    }

    const std::vector<Hit> hits = ScanVtables(process, module_base);
    std::array<std::size_t, kKinds.size()> counts{};
    for (const Hit& hit : hits) ++counts[hit.kind];
    for (std::size_t kind = 0; kind < kKinds.size(); ++kind) {
        if (counts[kind] != 0) {
            std::printf("%s=%zu\n", kKinds[kind].name, counts[kind]);
        }
    }

    for (const Hit& hit : hits) {
        const char* name = kKinds[hit.kind].name;
        std::printf("[%s] 0x%llX", name,
                    static_cast<unsigned long long>(hit.address));
        if (kKinds[hit.kind].rva == kControllerVtableRva) {
            PrintPointer(process, "status", hit.address + 0x18);
            PrintPointer(process, "unit", hit.address + 0x28);
        } else if (kKinds[hit.kind].rva == kTacticalStateVtableRva) {
            PrintPointer(process, "owner+28", hit.address + 0x28);
            PrintTacticsSubobject(process, hit.address + 0x28);
            PrintTacticsLinks(process, hit.address + 0x28);
            PrintPointer(process, "timer-list", hit.address + 0xB0);
        } else if (std::strncmp(name, "state_", 6) == 0) {
            PrintPointer(process, "controller", hit.address + 0x28);
            PrintMovementValues(process, hit.address);
        }
        std::putchar('\n');
    }

    CloseHandle(process);
    return 0;
}
