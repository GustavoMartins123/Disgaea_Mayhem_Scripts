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
    if (!pid) return 1;

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) return 1;

    HMODULE hMods[1024] = {};
    DWORD cbNeeded = 0;
    uintptr_t exe_base = 0;
    if (EnumProcessModules(hProc, hMods, sizeof(hMods), &cbNeeded)) {
        exe_base = (uintptr_t)hMods[0];
    }

    SYSTEM_INFO si = {};
    GetSystemInfo(&si);

    // Let's inspect the two active matches 0x00007FFFABB07104 and 0x00007FFFABB08084
    uintptr_t targets[] = { 0x00007FFFABB07104, 0x00007FFFABB08084 };

    for (int t = 0; t < 2; ++t) {
        uintptr_t base_struct = targets[t] - 0x104; // check if 0x100 is offset
        printf("\n=======================================================\n");
        printf("Inspecting Struct around 0x%016llX (Target 0x%016llX):\n",
               (unsigned long long)base_struct, (unsigned long long)targets[t]);
        printf("=======================================================\n");

        uint8_t buffer[0x200] = {};
        ReadProcessMemory(hProc, (LPCVOID)(targets[t] - 0x100), buffer, sizeof(buffer), NULL);

        for (size_t off = 0; off < sizeof(buffer); off += 16) {
            printf("[-0x%03zX / +0x%03zX]: ", 0x100 - off, off);
            for (size_t col = 0; col < 16; ++col) {
                printf("%02X ", buffer[off + col]);
            }
            printf(" | ");
            // Print as 64-bit pointers
            uintptr_t p1 = *(uintptr_t*)(buffer + off);
            uintptr_t p2 = *(uintptr_t*)(buffer + off + 8);
            printf("P1=0x%016llX P2=0x%016llX\n", (unsigned long long)p1, (unsigned long long)p2);
        }
    }

    // Now search for all pointers pointing to targets[0] and targets[1]
    printf("\n=== Scanning for pointers to 0x7FFFABB07104 / 0x7FFFABB08084 ===\n");
    uintptr_t curr = 0x10000;
    uintptr_t max_addr = (uintptr_t)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi = {};

    while (curr < max_addr && VirtualQueryEx(hProc, (LPCVOID)curr, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_READWRITE)) {
            std::vector<uint8_t> mem(mbi.RegionSize);
            SIZE_T bytesRead = 0;
            if (ReadProcessMemory(hProc, mbi.BaseAddress, mem.data(), mbi.RegionSize, &bytesRead)) {
                for (size_t i = 0; i + 8 <= bytesRead; i += 8) {
                    uintptr_t ptr = *(uintptr_t*)(mem.data() + i);
                    for (int t = 0; t < 2; ++t) {
                        if (ptr >= targets[t] - 0x200 && ptr <= targets[t] + 0x200) {
                            uintptr_t ptr_loc = (uintptr_t)mbi.BaseAddress + i;
                            int64_t rva = (int64_t)ptr_loc - (int64_t)exe_base;
                            if (rva > 0 && rva < 0x2000000) {
                                printf("[STATIC POINTER IN EXE] Disgaea_Mayhem.exe + 0x%llX -> 0x%016llX (Target %d: diff %+lld)\n",
                                       (unsigned long long)rva, (unsigned long long)ptr, t, (long long)(ptr - targets[t]));
                            } else {
                                printf("[HEAP PTR] 0x%016llX -> 0x%016llX (Target %d: diff %+lld)\n",
                                       (unsigned long long)ptr_loc, (unsigned long long)ptr, t, (long long)(ptr - targets[t]));
                            }
                        }
                    }
                }
            }
        }
        curr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }

    CloseHandle(hProc);
    return 0;
}
