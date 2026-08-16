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

    auto offset_to_va = [&](size_t off) -> uint64_t {
        if (off >= 0x400 && off < 0x400 + 0x9EB800) {
            return 0x140000000 + 0x1000 + (off - 0x400);
        }
        return 0;
    };

    printf("Searching for writes to [reg + 0x178] in code...\n");
    // Look for `sub DWORD PTR [r* + 0x178], ...` or `mov DWORD PTR [r* + 0x178], ...`
    // Opcode pattern: 89 ?? 78 01 00 00 (mov [r+0x178], reg) or 29 ?? 78 01 00 00 (sub [r+0x178], reg)
    for (size_t i = 0x400; i < 0x400 + 0x9EB800 - 6; ++i) {
        if ((data[i] == 0x89 || data[i] == 0x29 || data[i] == 0xFF || data[i] == 0x83) &&
            data[i+2] == 0x78 && data[i+3] == 0x01 && data[i+4] == 0x00 && data[i+5] == 0x00) {
            uint64_t va = offset_to_va(i);
            printf("Instruction at VA 0x%016llX: %02X %02X %02X %02X %02X %02X\n",
                (unsigned long long)va, data[i], data[i+1], data[i+2], data[i+3], data[i+4], data[i+5]);
        }
    }

    free(data);
    return 0;
}
