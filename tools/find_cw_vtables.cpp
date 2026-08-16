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

    auto va_to_offset = [&](uint64_t va) -> size_t {
        if (va >= 0x140A02000) {
            return (size_t)(va - 0x140A02000 + 0xA00E00);
        } else if (va >= 0x140001000) {
            return (size_t)(va - 0x140001000 + 0x400);
        }
        return 0;
    };

    // Find all type descriptors with "CharacterWorld"
    const char* pattern = ".?AV";
    for (size_t i = 0; i < sz - 64; ++i) {
        if (memcmp(data + i, pattern, 4) == 0) {
            const char* name = (const char*)(data + i + 4);
            if (strstr(name, "CharacterWorld") || strstr(name, "Sugoroku")) {
                uint64_t type_desc_va = 0x140A02000 + (i - 0xA00E00);
                // Search for references to this type_desc_va in .rdata (CompleteObjectLocator)
                for (size_t j = 0; j < sz - 8; ++j) {
                    if (*(uint32_t*)(data + j) == (uint32_t)(type_desc_va - 0x140000000)) {
                        // Found RVA in COL! COL is j - 12
                        uint64_t col_va = 0x140A02000 + ((j - 12) - 0xA00E00);
                        // Search for pointer to col_va in .rdata (vtable is right after)
                        for (size_t k = 0; k < sz - 8; ++k) {
                            if (*(uint64_t*)(data + k) == col_va) {
                                uint64_t vtable_va = 0x140A02000 + ((k + 8) - 0xA00E00);
                                printf("Class: %-50s | VTable: 0x%016llX | TypeDesc: 0x%016llX\n",
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
