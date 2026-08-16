#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <vector>

int main() {
    DWORD pid = 0;
    PROCESSENTRY32W pe = { sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"Disgaea_Mayhem.exe") == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }

    if (!pid) {
        printf("Disgaea_Mayhem.exe not running!\n");
        return 1;
    }
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) return 1;

    HMODULE hMods[1024] = {};
    DWORD cbNeeded = 0;
    uintptr_t exe_base = 0;
    if (EnumProcessModules(hProc, hMods, sizeof(hMods), &cbNeeded)) {
        exe_base = (uintptr_t)hMods[0];
    }
    printf("[PID %lu] EXE Base: 0x%016llX\n", pid, (unsigned long long)exe_base);

    SYSTEM_INFO si = {};
    GetSystemInfo(&si);

    uintptr_t curr = 0x10000;
    uintptr_t max_addr = (uintptr_t)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi = {};

    printf("=== Searching for Live Chara World Energy in Process ===\n");

    int found_count = 0;
    while (curr < max_addr && VirtualQueryEx(hProc, (LPCVOID)curr, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_READWRITE) && !(mbi.Protect & PAGE_GUARD)) {
            std::vector<uint8_t> buffer(mbi.RegionSize);
            SIZE_T bytesRead = 0;
            if (ReadProcessMemory(hProc, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &bytesRead) && bytesRead >= 16) {
                for (size_t i = 0; i + 16 <= bytesRead; i += 4) {
                    int32_t* p = (int32_t*)(buffer.data() + i);
                    if (p[0] >= 1 && p[0] <= 100 && p[1] == 100 && p[2] == 0) {
                        uintptr_t addr = (uintptr_t)mbi.BaseAddress + i;
                        printf("\n>>> MATCH [%d] at Addr: 0x%016llX | Energy=%d, Max=%d, Next=%d\n",
                               ++found_count, (unsigned long long)addr, p[0], p[1], p[2]);

                        // Dump 128 bytes around this address
                        uint8_t dump[256] = {};
                        ReadProcessMemory(hProc, (LPCVOID)(addr - 64), dump, 256, NULL);
                        printf("    Context bytes (-64 to +192):\n");
                        for (int row = 0; row < 8; ++row) {
                            printf("    [%+04d] ", row * 16 - 64);
                            for (int col = 0; col < 16; ++col) {
                                printf("%02X ", dump[row * 16 + col]);
                            }
                            printf("\n");
                        }

                        // Let's test writing 100 to energy right now!
                        int32_t full_energy = 100;
                        if (WriteProcessMemory(hProc, (LPVOID)addr, &full_energy, 4, NULL)) {
                            printf("    [SUCCESS] Wrote 100 to Energy at 0x%016llX!\n", (unsigned long long)addr);
                        }
                    }
                }
            }
        }
        curr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }

    printf("\nTotal Matching Blocks Found: %d\n", found_count);
    CloseHandle(hProc);
    return 0;
}
