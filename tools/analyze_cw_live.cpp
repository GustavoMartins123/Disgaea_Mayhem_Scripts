#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int main() {
    SetConsoleOutputCP(CP_UTF8);

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    DWORD pid = 0;
    if (Process32First(hSnap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "Disgaea_Mayhem.exe") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(hSnap, &pe));
    }
    CloseHandle(hSnap);

    if (!pid) {
        printf("Disgaea_Mayhem.exe not running\n");
        return 1;
    }

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        printf("Failed to open process\n");
        return 1;
    }

    // Get exe base
    uintptr_t exe_base = 0x140000000;
    HANDLE hModSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hModSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 me = { sizeof(me) };
        if (Module32First(hModSnap, &me)) {
            exe_base = (uintptr_t)me.modBaseAddr;
        }
        CloseHandle(hModSnap);
    }
    printf("Game PID: %lu, Base: 0x%016llX\n", pid, (unsigned long long)exe_base);

    // Scan for pattern: 4 consecutive ints with energy values (e.g. 10..100) or find memory regions
    MEMORY_BASIC_INFORMATION mbi = {};
    uintptr_t address = 0;
    int found_blocks = 0;

    while (VirtualQueryEx(hProcess, (LPCVOID)address, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
            uint8_t* buf = (uint8_t*)malloc(mbi.RegionSize);
            SIZE_T br = 0;
            if (buf && ReadProcessMemory(hProcess, (LPCVOID)address, buf, mbi.RegionSize, &br)) {
                if (br >= 32) {
                    for (size_t i = 0; i <= br - 32; i += 4) {
                        int32_t v0 = *(int32_t*)(buf + i);
                        int32_t v1 = *(int32_t*)(buf + i + 8);
                        int32_t v2 = *(int32_t*)(buf + i + 12);
                        int32_t v3 = *(int32_t*)(buf + i + 16);

                        // If v0 == v1 == v2 == v3 (or close) and in range 10..100 (current energy)
                        if (v0 > 0 && v0 <= 100 && v0 == v1 && v0 == v2 && v0 == v3) {
                            uintptr_t match_va = address + i;
                            printf("\n[MATCH] Energy Candidate at 0x%016llX (Value: %d)\n", (unsigned long long)match_va, v0);

                            // Dump 128 bytes before and after to inspect object header and VTable
                            size_t obj_start_offset = (i >= 0x820) ? (i - 0x810) : 0;
                            uintptr_t possible_vtable = *(uintptr_t*)(buf + obj_start_offset);
                            printf("  Possible Obj Base at 0x%016llX, VTable: 0x%016llX\n",
                                (unsigned long long)(address + obj_start_offset),
                                (unsigned long long)possible_vtable);

                            // Dump around match
                            printf("  Nearby bytes (offset -16 to +32):\n  ");
                            size_t dump_start = (i >= 16) ? (i - 16) : 0;
                            for (size_t k = dump_start; k < i + 32 && k < br; ++k) {
                                printf("%02X ", buf[k]);
                            }
                            printf("\n");

                            found_blocks++;
                        }
                    }
                }
                free(buf);
            }
        }
        address += mbi.RegionSize;
    }

    printf("\nTotal Energy candidate clusters found: %d\n", found_blocks);
    CloseHandle(hProcess);
    return 0;
}
