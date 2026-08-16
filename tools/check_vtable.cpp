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

    uint64_t vtable_cw = 0x140A57610;
    size_t off = va_to_offset(vtable_cw);
    printf("VTable CW 0x140A57610 -> file offset: 0x%zX\n", off);
    if (off) {
        printf("First 4 virtual functions:\n");
        for (int i = 0; i < 4; ++i) {
            uint64_t fn = *(uint64_t*)(data + off + i * 8);
            printf("  [Slot %d] 0x%016llX\n", i, (unsigned long long)fn);
        }
    }

    free(data);
    return 0;
}
