#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int main() {
    FILE* f = fopen("E:/Steam/steamapps/common/Disgaea Mayhem/Disgaea_Mayhem.exe", "rb");
    if (!f) return 1;

    IMAGE_DOS_HEADER dos;
    fread(&dos, sizeof(dos), 1, f);
    fseek(f, dos.e_lfanew, SEEK_SET);
    IMAGE_NT_HEADERS64 nt;
    fread(&nt, sizeof(nt), 1, f);

    uint32_t rva = 0x1D76C0;
    uint32_t file_offset = 0x1D6AC0;

    fseek(f, file_offset, SEEK_SET);
    uint8_t code[256];
    fread(code, 1, sizeof(code), f);
    fclose(f);

    printf("Dump of CItemWorldData::onFloorClear (0x1401D76C0):\n");
    for (int i = 0; i < 128; i += 16) {
        printf("0x%08X: ", rva + i);
        for (int j = 0; j < 16; ++j) printf("%02X ", code[i+j]);
        printf("\n");
    }

    return 0;
}
