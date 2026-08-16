#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

void disassemble_range(uint8_t* code, uintptr_t base_va, size_t size) {
    for (size_t i = 0; i < size; ) {
        printf("0x%016llX: ", (unsigned long long)(base_va + i));
        size_t len = 8;
        if (i + len > size) len = size - i;
        for (size_t j = 0; j < len; ++j) {
            printf("%02X ", code[i + j]);
        }
        for (size_t j = len; j < 8; ++j) printf("   ");
        printf("\n");
        i += len;
    }
}

int main() {
    FILE* f = fopen("E:/Steam/steamapps/common/Disgaea Mayhem/Disgaea_Mayhem.exe", "rb");
    if (!f) {
        printf("Error opening exe\n");
        return 1;
    }
    
    // PE Header parsing
    IMAGE_DOS_HEADER dos;
    fread(&dos, sizeof(dos), 1, f);
    fseek(f, dos.e_lfanew, SEEK_SET);
    IMAGE_NT_HEADERS64 nt;
    fread(&nt, sizeof(nt), 1, f);

    IMAGE_SECTION_HEADER sections[32];
    fread(sections, sizeof(IMAGE_SECTION_HEADER), nt.FileHeader.NumberOfSections, f);

    auto rva_to_file_offset = [&](uint32_t rva) -> uint32_t {
        for (int i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            if (rva >= sections[i].VirtualAddress && rva < sections[i].VirtualAddress + sections[i].Misc.VirtualSize) {
                return sections[i].PointerToRawData + (rva - sections[i].VirtualAddress);
            }
        }
        return 0;
    };

    uintptr_t targets[] = {
        0x1401D76C0, // CItemWorldData floor clear / update
        0x1403FE040, // CState_Item@CTask_Explore_ItemWorldClear
        0x1401CFB00, // CItemStatus::setLevel
        0x1401CEA80, // CItemStatus::recalculateStats
        0x1403FDCB0  // ItemWorld result display
    };

    for (uintptr_t target : targets) {
        uint32_t rva = (uint32_t)(target - nt.OptionalHeader.ImageBase);
        uint32_t foff = rva_to_file_offset(rva);
        printf("\n============================================================\n");
        printf("Target VA: 0x%016llX (RVA: 0x%X, FileOffset: 0x%X)\n", (unsigned long long)target, rva, foff);
        printf("============================================================\n");
        if (foff) {
            fseek(f, foff, SEEK_SET);
            uint8_t buffer[128];
            fread(buffer, 1, sizeof(buffer), f);
            disassemble_range(buffer, target, sizeof(buffer));
        }
    }

    fclose(f);
    return 0;
}
