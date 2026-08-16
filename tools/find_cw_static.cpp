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

    // Find references to CTask_CharacterWorldGame_Turn / CCharacterWorldManager
    // In Disgaea Mayhem, CTaskManager static instance is at a fixed address.
    // Let's find RIP-relative LEA/MOV references to .data (.data is 0x140A02000 to 0x140C00000)
    printf("Scanning for static pointers to Character World and Item World...\n");

    for (size_t i = 0x400; i < 0xA00000 - 7; ++i) {
        // MOV reg, [RIP + disp32] (48 8B 05 xx xx xx xx)
        if (data[i] == 0x48 && data[i+1] == 0x8B && (data[i+2] == 0x05 || data[i+2] == 0x0D || data[i+2] == 0x15 || data[i+2] == 0x1D)) {
            int32_t disp = *(int32_t*)(data + i + 3);
            uint64_t rip = 0x140001000 + (i - 0x400) + 7;
            uint64_t target_va = rip + disp;
            if (target_va >= 0x140CB0000 && target_va <= 0x140CF0000) {
                // Check if this function is near CW functions (e.g. 0x140450000 to 0x1404F0000)
                if (rip >= 0x140450000 && rip <= 0x1404EF000) {
                    printf("CW Static Ref at VA 0x%016llX -> Target VA 0x%016llX\n", (unsigned long long)rip, (unsigned long long)target_va);
                }
            }
        }
    }

    free(data);
    return 0;
}
