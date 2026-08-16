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
    DWORD exe_size = 0;
    if (hModSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 me = { sizeof(me) };
        if (Module32First(hModSnap, &me)) {
            exe_base = (uintptr_t)me.modBaseAddr;
            exe_size = me.modBaseSize;
        }
        CloseHandle(hModSnap);
    }

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) return 1;

    uintptr_t target_obj = 0x0000027D680610C0; // CCharacterWorldInformation
    uintptr_t target_ui = 0x0000027D6476F7A0;  // CUIUnion_CharacterWorld_Energy

    printf("Module Base: 0x%016llX, Size: 0x%X\n", (unsigned long long)exe_base, exe_size);
    printf("Searching for pointers to CCharacterWorldInformation (0x%016llX) and UI (0x%016llX)...\n",
        (unsigned long long)target_obj, (unsigned long long)target_ui);

    // Read all of exe memory (.data, .bss, etc.)
    uint8_t* exe_buf = (uint8_t*)malloc(exe_size);
    SIZE_T br = 0;
    if (ReadProcessMemory(hProcess, (LPCVOID)exe_base, exe_buf, exe_size, &br)) {
        for (size_t i = 0; i <= br - 8; i += 8) {
            uintptr_t ptr = *(uintptr_t*)(exe_buf + i);
            if (ptr == target_obj) {
                printf("[STATIC PTR 1-LEVEL] Disgaea_Mayhem.exe + 0x%08zX -> CCharacterWorldInformation\n", i);
            }
            if (ptr == target_ui) {
                printf("[STATIC PTR 1-LEVEL] Disgaea_Mayhem.exe + 0x%08zX -> CUIUnion_CharacterWorld_Energy\n", i);
            }
        }
    }
    free(exe_buf);

    CloseHandle(hProcess);
    return 0;
}
