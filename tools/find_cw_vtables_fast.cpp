#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

int main() {
    FILE* f = fopen("E:/Steam/steamapps/common/Disgaea Mayhem/Disgaea_Mayhem.exe", "rb");
    if (!f) return 1;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = (uint8_t*)malloc(sz);
    fread(data, 1, sz, f);
    fclose(f);

    const char* targets[] = {
        "CUIUnion_CharacterWorld_Energy",
        "CUIUnion_CharacterWorld_FluctuationWindow_Energy",
        "CUIUnion_CharacterWorldBattle_Energy",
        "CTask_CharacterWorldGame_Move",
        "CTask_CharacterWorldGame_Dice",
        "CTaskSettingData_CharacterWorldGame_Move",
        "CSugorokuMapInformation",
        "CSugorokuCellInformation",
        "CSugorokuEventData"
    };

    printf("============================================================\n");
    printf("  FINDING CHARACTER WORLD / SUGOROKU VTABLES\n");
    printf("============================================================\n");

    for (int t = 0; t < 9; ++t) {
        // 1. Find string in data
        const char* name = targets[t];
        for (size_t i = 0; i < sz - strlen(name) - 8; ++i) {
            if (memcmp(data + i, name, strlen(name)) == 0 && data[i-1] == 'V' && data[i-2] == 'A' && data[i-3] == '?') {
                // TypeDescriptor starts at i - 16
                uint32_t type_desc_rva = (uint32_t)(0xA02000 + ((i - 16) - 0xA00E00));
                uint64_t type_desc_va = 0x140000000 + type_desc_rva;

                // 2. Search for type_desc_rva in .rdata (CompleteObjectLocator)
                for (size_t j = 0; j < sz - 24; ++j) {
                    if (*(uint32_t*)(data + j) == type_desc_rva) {
                        // CompleteObjectLocator is at j - 12
                        uint64_t col_va = 0x140000000 + (0xA02000 + ((j - 12) - 0xA00E00));

                        // 3. Search for pointer to col_va
                        for (size_t k = 0; k < sz - 8; ++k) {
                            if (*(uint64_t*)(data + k) == col_va) {
                                uint64_t vtable_va = 0x140000000 + (0xA02000 + ((k + 8) - 0xA00E00));
                                printf("[FOUND] %-45s | VTable: 0x%016llX | TypeDesc: 0x%016llX\n",
                                    name, (unsigned long long)vtable_va, (unsigned long long)type_desc_va);
                            }
                        }
                    }
                }
            }
        }
    }

    free(data);
    return 0;
}
