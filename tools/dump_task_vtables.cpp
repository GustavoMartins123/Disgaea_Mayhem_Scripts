#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

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
        // .rdata starts at RVA 0xA02000, FileOffset 0xA00E00
        // .text starts at RVA 0x1000, FileOffset 0x400
        if (va >= 0x140A02000) {
            return (size_t)(va - 0x140A02000 + 0xA00E00);
        } else if (va >= 0x140001000) {
            return (size_t)(va - 0x140001000 + 0x400);
        }
        return 0;
    };

    uint64_t vtables[] = {
        0x140A4E320, // CTask_Explore_ItemWorldClear
        0x140A4E1D0, // CState_Main@CTask_Explore_ItemWorldClear
        0x140A4DFA0, // CState_Item@CTask_Explore_ItemWorldClear
        0x140A4E0E0, // CState_Performance@CTask_Explore_ItemWorldClear
        0x140A251F0, // CItemWorldData
        0x140A252C0  // CItemStatus
    };

    const char* names[] = {
        "CTask_Explore_ItemWorldClear",
        "CState_Main@CTask_Explore_ItemWorldClear",
        "CState_Item@CTask_Explore_ItemWorldClear",
        "CState_Performance@CTask_Explore_ItemWorldClear",
        "CItemWorldData",
        "CItemStatus"
    };

    for (int k = 0; k < 6; ++k) {
        printf("\n============================================================\n");
        printf("VTable %s (0x%016llX):\n", names[k], (unsigned long long)vtables[k]);
        printf("============================================================\n");
        size_t off = va_to_offset(vtables[k]);
        if (off && off + 80 <= sz) {
            for (int i = 0; i < 10; ++i) {
                uint64_t fn_va = *(uint64_t*)(data + off + i * 8);
                printf("  [Slot %2d] VA: 0x%016llX\n", i, (unsigned long long)fn_va);
            }
        }
    }

    free(data);
    return 0;
}
