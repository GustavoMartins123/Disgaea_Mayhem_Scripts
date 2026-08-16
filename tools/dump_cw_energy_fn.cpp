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
        if (va >= 0x140001000) return (size_t)(va - 0x140001000 + 0x400);
        return 0;
    };

    uint64_t start_va = 0x140461E70;
    size_t off = va_to_offset(start_va);

    printf("Bytes around 0x140461E70 (CharacterWorld energy check/decrement):\n");
    for (size_t i = 0; i < 256; ++i) {
        if (i % 16 == 0) printf("\n0x%016llX: ", (unsigned long long)(start_va + i));
        printf("%02X ", data[off + i]);
    }
    printf("\n");

    free(data);
    return 0;
}
