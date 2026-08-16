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

    uintptr_t target_va = 0x1401D76C0;
    uintptr_t image_base = 0x140000000;

    for (size_t i = 0; i < sz - 7; ++i) {
        // Check 4-byte displacement in RIP relative: lea / mov / jmp
        int32_t disp = *(int32_t*)(data + i + 3);
        uintptr_t cur_va = image_base + 0x1000 + (i - 0x400);
        uintptr_t rip = cur_va + 7;
        if (rip + disp == target_va) {
            printf("RIP-relative ref (7-byte) to 0x1401D76C0 at VA 0x%016llX (opcode: %02X %02X %02X)\n",
                (unsigned long long)cur_va, data[i], data[i+1], data[i+2]);
        }
    }

    free(data);
    return 0;
}
