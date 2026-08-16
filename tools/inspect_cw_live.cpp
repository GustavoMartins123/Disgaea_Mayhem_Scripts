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

    if (!pid) return 1;

    HANDLE hModSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    uintptr_t exe_base = 0;
    if (hModSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 me = { sizeof(me) };
        if (Module32First(hModSnap, &me)) {
            exe_base = (uintptr_t)me.modBaseAddr;
        }
        CloseHandle(hModSnap);
    }
    printf("Game PID: %lu, Module Base: 0x%016llX\n", pid, (unsigned long long)exe_base);

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) return 1;

    uintptr_t vtable_cw_info = exe_base + 0xA57610;
    uintptr_t vtable_cw_energy_ui = exe_base + 0xA71728;
    uintptr_t vtable_cw_bonus = exe_base + 0xA57620;
    uintptr_t vtable_cw_setting = exe_base + 0xA57600;

    printf("Searching for:\n");
    printf("  vtable CCharacterWorldInformation = 0x%016llX\n", (unsigned long long)vtable_cw_info);
    printf("  vtable CUIUnion_CharacterWorld_Energy = 0x%016llX\n", (unsigned long long)vtable_cw_energy_ui);

    MEMORY_BASIC_INFORMATION mbi = {};
    uintptr_t address = 0;

    while (VirtualQueryEx(hProcess, (LPCVOID)address, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
            uint8_t* buf = (uint8_t*)malloc(mbi.RegionSize);
            SIZE_T br = 0;
            if (buf && ReadProcessMemory(hProcess, (LPCVOID)address, buf, mbi.RegionSize, &br)) {
                if (br >= 128) {
                    for (size_t i = 0; i <= br - 128; i += 8) {
                        uintptr_t vptr = *(uintptr_t*)(buf + i);
                        if (vptr == vtable_cw_info) {
                            uintptr_t obj_va = address + i;
                            printf("\n============================================================\n");
                            printf("[FOUND] CCharacterWorldInformation instance at 0x%016llX\n", (unsigned long long)obj_va);
                            printf("============================================================\n");
                            for (size_t off = 0; off < 0x200 && (i + off + 4 <= br); off += 4) {
                                int32_t val_i32 = *(int32_t*)(buf + i + off);
                                uint32_t val_u32 = *(uint32_t*)(buf + i + off);
                                if (val_i32 > 0 && val_i32 <= 9999) {
                                    printf("  +0x%04zX : %6d (0x%08X)\n", off, val_i32, val_u32);
                                }
                            }
                        } else if (vptr == vtable_cw_energy_ui) {
                            uintptr_t obj_va = address + i;
                            printf("\n[FOUND] CUIUnion_CharacterWorld_Energy instance at 0x%016llX\n", (unsigned long long)obj_va);
                            for (size_t off = 0; off < 0x100 && (i + off + 4 <= br); off += 4) {
                                int32_t val_i32 = *(int32_t*)(buf + i + off);
                                if (val_i32 > 0 && val_i32 <= 9999) {
                                    printf("  +0x%04zX : %6d\n", off, val_i32);
                                }
                            }
                        }
                    }
                }
                free(buf);
            }
        }
        address += mbi.RegionSize;
    }

    CloseHandle(hProcess);
    return 0;
}
