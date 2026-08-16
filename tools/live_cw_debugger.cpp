#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <vector>

struct Candidate {
    uintptr_t addr;
    uintptr_t vptr;
    int32_t val0;
    int32_t val1;
    int32_t val2;
    int32_t val3;
};

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
    if (!hProc) {
        printf("OpenProcess failed (%lu)\n", GetLastError());
        return 1;
    }

    HMODULE hMods[1024] = {};
    DWORD cbNeeded = 0;
    uintptr_t exe_base = 0;
    if (EnumProcessModules(hProc, hMods, sizeof(hMods), &cbNeeded)) {
        exe_base = (uintptr_t)hMods[0];
    }
    printf("[PID %lu] EXE Base: 0x%016llX\n", pid, (unsigned long long)exe_base);

    // Let's scan all committed memory in the process
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);

    uintptr_t curr = 0x10000;
    uintptr_t max_addr = (uintptr_t)si.lpMaximumApplicationAddress;

    MEMORY_BASIC_INFORMATION mbi = {};
    std::vector<Candidate> results;

    printf("Scanning memory for Chara World Energy blocks...\n");

    while (curr < max_addr && VirtualQueryEx(hProc, (LPCVOID)curr, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_READWRITE) && !(mbi.Protect & PAGE_GUARD)) {
            std::vector<uint8_t> buffer(mbi.RegionSize);
            SIZE_T bytesRead = 0;
            if (ReadProcessMemory(hProc, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &bytesRead) && bytesRead > 32) {
                for (size_t i = 0; i + 32 <= bytesRead; i += 4) {
                    int32_t* p = (int32_t*)(buffer.data() + i);
                    // Match Cheat Table pattern:
                    // val0 (at offset 0), val1 (energy, 1..100), val2 (max energy, 100), val3 (turns/dice)
                    // Or check if energy is between 1 and 100 with max energy = 100
                    int32_t e_cur = p[0];
                    int32_t e_max = p[1];
                    if (e_max == 100 && e_cur >= 0 && e_cur <= 100) {
                        uintptr_t candidate_addr = (uintptr_t)mbi.BaseAddress + i;
                        // Read first 8 bytes (vptr) of the enclosing struct
                        // Let's check 0x10, 0x20, 0x178, 0x800 behind
                        uintptr_t struct_base = (candidate_addr & ~0x0F);
                        uintptr_t vptr = 0;
                        ReadProcessMemory(hProc, (LPCVOID)struct_base, &vptr, sizeof(vptr), NULL);

                        Candidate c = {};
                        c.addr = candidate_addr;
                        c.vptr = vptr;
                        c.val0 = p[0];
                        c.val1 = p[1];
                        c.val2 = p[2];
                        c.val3 = p[3];
                        results.push_back(c);
                    }
                }
            }
        }
        curr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }

    printf("Found %zu candidate energy blocks in memory!\n", results.size());
    for (size_t i = 0; i < results.size() && i < 30; ++i) {
        printf("[%2zu] Addr: 0x%016llX | Vals: %d, %d, %d, %d | Vptr at Base: 0x%016llX\n",
               i,
               (unsigned long long)results[i].addr,
               results[i].val0, results[i].val1, results[i].val2, results[i].val3,
               (unsigned long long)results[i].vptr);
    }

    // Check pointers pointing to these candidates
    printf("\nChecking pointer references in memory to candidate addresses...\n");
    for (size_t c = 0; c < results.size() && c < 5; ++c) {
        uintptr_t target = results[c].addr;
        uintptr_t target_aligned = (target & ~0xFF);

        curr = 0x10000;
        while (curr < max_addr && VirtualQueryEx(hProc, (LPCVOID)curr, &mbi, sizeof(mbi))) {
            if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_READWRITE)) {
                std::vector<uint8_t> buffer(mbi.RegionSize);
                SIZE_T bytesRead = 0;
                if (ReadProcessMemory(hProc, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &bytesRead)) {
                    for (size_t i = 0; i + 8 <= bytesRead; i += 8) {
                        uintptr_t ptr = *(uintptr_t*)(buffer.data() + i);
                        if (ptr >= target_aligned && ptr <= target + 0x100) {
                            uintptr_t ptr_addr = (uintptr_t)mbi.BaseAddress + i;
                            int64_t rva = (int64_t)ptr_addr - (int64_t)exe_base;
                            if (rva > 0 && rva < 0x2000000) {
                                printf(">>> STATIC EXE POINTER: Disgaea_Mayhem.exe + 0x%llX -> 0x%llX\n",
                                       (unsigned long long)rva, (unsigned long long)ptr);
                            } else {
                                printf("    Heap Pointer at 0x%016llX -> 0x%016llX\n",
                                       (unsigned long long)ptr_addr, (unsigned long long)ptr);
                            }
                        }
                    }
                }
            }
            curr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        }
    }

    CloseHandle(hProc);
    return 0;
}
