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

    IMAGE_SECTION_HEADER sections[32];
    fread(sections, sizeof(IMAGE_SECTION_HEADER), nt.FileHeader.NumberOfSections, f);

    printf("Sections of Disgaea_Mayhem.exe:\n");
    for (int i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        printf("  %-8s | RVA: 0x%08X | VirtSize: 0x%08X | RawOffset: 0x%08X | RawSize: 0x%08X\n",
            sections[i].Name, sections[i].VirtualAddress, sections[i].Misc.VirtualSize,
            sections[i].PointerToRawData, sections[i].SizeOfRawData);
    }

    fclose(f);
    return 0;
}
