#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <vector>

int main() {
    FILE* f = fopen("E:/Steam/steamapps/common/Disgaea Mayhem/Disgaea_Mayhem.exe", "rb");
    if (!f) return 1;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = (uint8_t*)malloc(sz);
    fread(data, 1, sz, f);
    fclose(f);

    printf("Analyzing all SUB / DEC / MOV instructions modifying Energy in Disgaea_Mayhem.exe...\n");

    // Look for instructions like `sub [reg + offset], eax` or `dec dword ptr [reg + offset]` or `mov [reg + offset], eax`
    // where offset is known CW offset, or functions in 0x140450000 - 0x1404E0000 range
    for (size_t i = 0x400; i < 0x9EB000; ++i) {
        uint64_t va = 0x140001000 + (i - 0x400);
        if (va >= 0x140450000 && va <= 0x1404E0000) {
            // sub dword ptr [rcx + disp8], edx (29 51 xx)
            // sub [reg + disp8], reg
            // 29 48 .. 29 50 ..
            // 83 68 xx yy (sub dword ptr [rax+xx], yy)
            // 83 69 xx yy (sub dword ptr [rcx+xx], yy)
            // FF 48 xx (dec dword ptr [rax+xx])
            // FF 49 xx (dec dword ptr [rcx+xx])
            if (data[i] == 0x83 && (data[i+1] == 0x69 || data[i+1] == 0x68 || data[i+1] == 0x6B || data[i+1] == 0x6F)) {
                printf("[SUB CONST] VA 0x%016llX: %02X %02X %02X %02X\n",
                       (unsigned long long)va, data[i], data[i+1], data[i+2], data[i+3]);
            }
            if (data[i] == 0xFF && (data[i+1] == 0x49 || data[i+1] == 0x48 || data[i+1] == 0x4B || data[i+1] == 0x4F || data[i+1] == 0x8B || data[i+1] == 0x89)) {
                printf("[DEC DWORD] VA 0x%016llX: %02X %02X %02X %02X %02X %02X\n",
                       (unsigned long long)va, data[i], data[i+1], data[i+2], data[i+3], data[i+4], data[i+5]);
            }
        }
    }

    free(data);
    return 0;
}
