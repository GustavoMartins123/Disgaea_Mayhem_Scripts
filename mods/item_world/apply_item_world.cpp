#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <algorithm>

int main() {
    SetConsoleOutputCP(CP_UTF8);

    printf("=================================================================\n");
    printf("  DISGAEA MAYHEM - MOD DE ITEM WORLD & INOCENTES (C++ NATIVE)\n");
    printf("=================================================================\n");

    int levels_per_floor = 5;
    bool auto_subdue = true;
    int mystery_room_rate = 75;

    // 1. Carregar mod.json se existir
    FILE* f = fopen("mod.json", "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0) {
            char* json = (char*)malloc(sz + 1);
            fread(json, 1, sz, f);
            json[sz] = 0;

            char* p = strstr(json, "\"levels_per_floor\"");
            if (p) {
                char* v = strstr(p, "\"value\":");
                if (v) levels_per_floor = atoi(v + 8);
            }
            p = strstr(json, "\"auto_subdue\"");
            if (p) {
                char* v = strstr(p, "\"value\":");
                if (v) {
                    if (strstr(v, "false")) auto_subdue = false;
                    else auto_subdue = true;
                }
            }
            p = strstr(json, "\"mystery_room_rate\"");
            if (p) {
                char* v = strstr(p, "\"value\":");
                if (v) mystery_room_rate = atoi(v + 8);
            }
            free(json);
        }
        fclose(f);
    }

    printf("[CONFIG] Parametros carregados:\n");
    printf("  -> Niveis por andar: +%d Lv\n", levels_per_floor);
    printf("  -> Subjugar Inocentes: %s (100%%)\n", auto_subdue ? "Sim" : "Nao");
    printf("  -> Taxa Mystery Rooms: %d%%\n\n", mystery_room_rate);

    // 2. Localizar processo Disgaea_Mayhem.exe
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        printf("[ERRO] Nao foi possivel criar snapshot dos processos.\n");
        return 1;
    }

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
        printf("[INFO] Disgaea_Mayhem.exe nao esta em execucao no momento.\n");
        printf("       As configuracoes estao salvas no mod.json para a proxima sessao.\n");
        printf("=================================================================\n");
        return 0;
    }

    printf("[OK] Jogo detectado em execucao (PID: %lu)\n", pid);
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        printf("[ERRO] Nao foi possivel abrir o processo do jogo com permissao de escrita.\n");
        return 1;
    }

    // 3. Obter base do modulo executavel
    uintptr_t exe_base = 0x140000000;
    HANDLE hModSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hModSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 me = { sizeof(me) };
        if (Module32First(hModSnap, &me)) {
            if (me.modBaseAddr) {
                exe_base = (uintptr_t)me.modBaseAddr;
            }
        }
        CloseHandle(hModSnap);
    }

    uintptr_t vtable_item_status = exe_base + 0xA252C0;
    uintptr_t vtable_item_world = exe_base + 0xA251F0;

    printf("[OK] Base do Executavel: 0x%016llX\n", (unsigned long long)exe_base);
    printf("[OK] VTable CItemStatus: 0x%016llX\n", (unsigned long long)vtable_item_status);
    printf("[OK] VTable CItemWorldData: 0x%016llX\n", (unsigned long long)vtable_item_world);

    // 4. Varredura e Injeção na Memória RAM
    MEMORY_BASIC_INFORMATION mbi = {};
    uintptr_t address = 0;
    int items_boosted = 0;
    int innocents_subdued = 0;
    int sessions_hooked = 0;

    while (VirtualQueryEx(hProcess, (LPCVOID)address, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
            uint8_t* buffer = (uint8_t*)malloc(mbi.RegionSize);
            SIZE_T bytes_read = 0;
            if (buffer && ReadProcessMemory(hProcess, (LPCVOID)address, buffer, mbi.RegionSize, &bytes_read)) {
                
                // 1. Modificar CItemWorldData
                if (bytes_read >= 0x100) {
                    for (size_t i = 0; i <= bytes_read - 0x100; i += 8) {
                        uintptr_t vptr = *(uintptr_t*)(buffer + i);
                        if (vptr == vtable_item_world) {
                            uintptr_t iw_addr = address + i;
                            uintptr_t p_level_inc = iw_addr + 0x74;
                            int32_t cur_inc = 0;
                            ReadProcessMemory(hProcess, (LPCVOID)p_level_inc, &cur_inc, 4, NULL);
                            if (cur_inc >= 0 && cur_inc <= 100) {
                                int32_t new_inc = levels_per_floor;
                                WriteProcessMemory(hProcess, (LPVOID)p_level_inc, &new_inc, 4, NULL);
                                sessions_hooked++;
                            }
                        }
                    }
                }

                // 2. Modificar CItemStatus & Inocentes
                if (bytes_read >= 0x380) {
                    for (size_t i = 0; i <= bytes_read - 0x380; i += 8) {
                        uintptr_t vptr = *(uintptr_t*)(buffer + i);
                        if (vptr == vtable_item_status) {
                            uintptr_t item_addr = address + i;
                            uint32_t ref_count = *(uint32_t*)(buffer + i + 8);
                            if (ref_count > 0 && ref_count < 100000) {
                                uintptr_t p_iw_lv = item_addr + 0x328;
                                uint16_t cur_lv = 0;
                                ReadProcessMemory(hProcess, (LPCVOID)p_iw_lv, &cur_lv, 2, NULL);
                                if (cur_lv < 9999) {
                                    uint16_t new_lv = (uint16_t)std::min(9999, cur_lv + levels_per_floor);
                                    WriteProcessMemory(hProcess, (LPVOID)p_iw_lv, &new_lv, 2, NULL);
                                    items_boosted++;
                                }

                                if (auto_subdue) {
                                    uintptr_t inno_start = *(uintptr_t*)(buffer + i + 0x358);
                                    uintptr_t inno_end = *(uintptr_t*)(buffer + i + 0x360);
                                    if (inno_start && inno_end >= inno_start && (inno_end - inno_start) <= 64 * 8) {
                                        for (uintptr_t p = inno_start; p < inno_end; p += 8) {
                                            uintptr_t inno_obj = 0;
                                            if (ReadProcessMemory(hProcess, (LPCVOID)p, &inno_obj, 8, NULL) && inno_obj) {
                                                uint32_t is_subdued = 0;
                                                int32_t power = 0;
                                                ReadProcessMemory(hProcess, (LPCVOID)(inno_obj + 0x14), &is_subdued, 4, NULL);
                                                ReadProcessMemory(hProcess, (LPCVOID)(inno_obj + 0x18), &power, 4, NULL);
                                                if (is_subdued == 0) {
                                                    uint32_t sub_one = 1;
                                                    WriteProcessMemory(hProcess, (LPVOID)(inno_obj + 0x14), &sub_one, 4, NULL);
                                                    if (power > 0) {
                                                        int32_t dbl_power = power * 2;
                                                        WriteProcessMemory(hProcess, (LPVOID)(inno_obj + 0x18), &dbl_power, 4, NULL);
                                                    }
                                                    innocents_subdued++;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                free(buffer);
            }
        }
        address += mbi.RegionSize;
    }

    CloseHandle(hProcess);

    printf("\n[SUCESSO] Operacao concluida via binario nativo C++!\n");
    printf("  -> Sessoes do Item World sincronizadas: %d\n", sessions_hooked);
    printf("  -> Itens evoluidos (+%d Lv): %d\n", levels_per_floor, items_boosted);
    printf("  -> Inocentes 100%% subjugados e maximizados: %d\n", innocents_subdued);
    printf("=================================================================\n");
    return 0;
}
