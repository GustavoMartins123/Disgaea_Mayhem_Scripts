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
        if (va >= 0x140A02000) return (size_t)(va - 0x140A02000 + 0xA00E00);
        if (va >= 0x140001000) return (size_t)(va - 0x140001000 + 0x400);
        return 0;
    };

    uint64_t vtables[] = {
        0x140A52E18, // CTask_CharacterWorldGame_TurnStart
        0x140A53D88, // CTask_CharacterWorldGame_Move
        0x140A57610  // CCharacterWorldInformation
    };
    const char* names[] = {
        "CTask_CharacterWorldGame_TurnStart",
        "CTask_CharacterWorldGame_Move",
        "CCharacterWorldInformation"
    };

    for (int k = 0; k < 3; ++k) {
        printf("\n============================================================\n");
        printf("VTable %s (0x%016llX):\n", names[k], (unsigned long long)vtables[k]);
        printf("============================================================\n");
        size_t off = va_to_offset(vtables[k]);
        if (off && off + 80 <= sz) {
            for (int i = 0; i < 12; ++i) {
                uint64_t fn_va = *(uint64_t*)(data + off + i * 8);
                printf("  [Slot %2d] VA: 0x%016llX\n", i, (unsigned long long)fn_va);
            }
        }
    }

    free(data);
    return 0;
}
