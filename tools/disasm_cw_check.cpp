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

    printf("\n=======================================================\n");
    printf("Bytes at VA 0x0000000140461E70:\n");
    printf("=======================================================\n");
    size_t off = va_to_offset(0x140461E70);
    if (off) {
        for (size_t i = 0; i < 128; ++i) {
            printf("%02X ", data[off + i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }
        printf("\n");
    }

    free(data);
    return 0;
}
