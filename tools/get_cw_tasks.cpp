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

    auto offset_to_rva = [&](size_t off) -> uint32_t {
        if (off >= 0x00C4FA00 && off < 0x00C4FA00 + 0x000D0A00) return (uint32_t)(off - 0x00C4FA00 + 0x00C51000);
        if (off >= 0x009EBC00 && off < 0x009EBC00 + 0x00263E00) return (uint32_t)(off - 0x009EBC00 + 0x009ED000);
        if (off >= 0x00000400 && off < 0x00000400 + 0x009EB800) return (uint32_t)(off - 0x00000400 + 0x00001000);
        return 0;
    };

    size_t data_start = 0x00C4FA00;
    size_t data_end = data_start + 0x000D0A00;
    size_t rdata_start = 0x009EBC00;
    size_t rdata_end = rdata_start + 0x00263E00;

    for (size_t i = data_start; i < data_end - 64; ++i) {
        if (memcmp(data + i, ".?AV", 4) == 0) {
            const char* name = (const char*)(data + i + 4);
            if (strstr(name, "CTask_CharacterWorld") || strstr(name, "CSugoroku") || strstr(name, "CCharacterWorld")) {
                uint32_t type_desc_rva = offset_to_rva(i - 16);
                for (size_t j = rdata_start; j < rdata_end - 24; j += 4) {
                    if (*(uint32_t*)(data + j) == type_desc_rva) {
                        uint32_t col_rva = offset_to_rva(j - 12);
                        uint64_t col_va = 0x140000000 + col_rva;
                        for (size_t k = rdata_start; k < rdata_end - 8; k += 8) {
                            if (*(uint64_t*)(data + k) == col_va) {
                                uint32_t vtable_rva = offset_to_rva(k + 8);
                                uint64_t vtable_va = 0x140000000 + vtable_rva;
                                printf("Task/Data Class: %-55s | VTable: 0x%016llX\n", name, (unsigned long long)vtable_va);
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
