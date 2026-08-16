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
        printf("Game not running\n");
        return 1;
    }

    HANDLE hModSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    uintptr_t exe_base = 0;
    if (hModSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 me = { sizeof(me) };
        if (Module32First(hModSnap, &me)) {
            exe_base = (uintptr_t)me.modBaseAddr;
        }
        CloseHandle(hModSnap);
    }

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) return 1;

    uintptr_t vtable_cw_info = exe_base + 0xA57610;
    uintptr_t vtable_cw_energy_ui = exe_base + 0xA71728;
    int32_t target_energy = 100;

    printf("Locking Chara World Energy to %d...\n", target_energy);

    MEMORY_BASIC_INFORMATION mbi = {};
    uintptr_t address = 0;
    int modified_objects = 0;

    while (VirtualQueryEx(hProcess, (LPCVOID)address, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
            uint8_t* buf = (uint8_t*)malloc(mbi.RegionSize);
            SIZE_T br = 0;
            if (buf && ReadProcessMemory(hProcess, (LPCVOID)address, buf, mbi.RegionSize, &br)) {
                if (br >= 0x200) {
                    for (size_t i = 0; i <= br - 0x200; i += 8) {
                        uintptr_t vptr = *(uintptr_t*)(buf + i);
                        if (vptr == vtable_cw_info) {
                            uintptr_t obj_va = address + i;
                            uintptr_t energy_addr = obj_va + 0x178;
                            WriteProcessMemory(hProcess, (LPVOID)energy_addr, &target_energy, sizeof(target_energy), NULL);
                            printf("[SUCCESS] Updated CCharacterWorldInformation at 0x%016llX -> Energy = %d\n",
                                (unsigned long long)obj_va, target_energy);
                            modified_objects++;
                        } else if (vptr == vtable_cw_energy_ui) {
                            uintptr_t obj_va = address + i;
                            WriteProcessMemory(hProcess, (LPVOID)(obj_va + 0x70), &target_energy, 4, NULL);
                            WriteProcessMemory(hProcess, (LPVOID)(obj_va + 0x78), &target_energy, 4, NULL);
                            WriteProcessMemory(hProcess, (LPVOID)(obj_va + 0x7C), &target_energy, 4, NULL);
                            WriteProcessMemory(hProcess, (LPVOID)(obj_va + 0x80), &target_energy, 4, NULL);
                            printf("[SUCCESS] Updated CUIUnion_CharacterWorld_Energy at 0x%016llX -> UI Energy = %d\n",
                                (unsigned long long)obj_va, target_energy);
                            modified_objects++;
                        }
                    }
                }
                free(buf);
            }
        }
        address += mbi.RegionSize;
    }

    printf("Finished! Total modified objects: %d\n", modified_objects);
    CloseHandle(hProcess);
    return 0;
}
