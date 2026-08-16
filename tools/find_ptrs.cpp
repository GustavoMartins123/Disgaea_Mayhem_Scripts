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

    uint64_t target_va = 0x1401D76C0;

    for (size_t i = 0; i <= sz - 8; ++i) {
        if (*(uint64_t*)(data + i) == target_va) {
            printf("Pointer to 0x1401D76C0 at file offset: 0x%zX\n", i);
        }
    }

    free(data);
    return 0;
}
