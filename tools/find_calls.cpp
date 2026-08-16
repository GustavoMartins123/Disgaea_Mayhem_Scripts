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

    // Scan for direct calls E8 rel32
    for (size_t i = 0; i < sz - 5; ++i) {
        if (data[i] == 0xE8) {
            int32_t rel = *(int32_t*)(data + i + 1);
            // file offset to VA: for .text section
            // In Disgaea_Mayhem.exe, .text RVA = 0x1000, FileOffset = 0x400 (diff = 0xC00)
            uintptr_t cur_va = image_base + 0x1000 + (i - 0x400);
            uintptr_t dest_va = cur_va + 5 + rel;
            if (dest_va == target_va) {
                printf("Direct CALL to 0x1401D76C0 at VA: 0x%016llX (file offset: 0x%zX)\n", 
                    (unsigned long long)cur_va, i);
            }
        }
    }

    free(data);
    return 0;
}
