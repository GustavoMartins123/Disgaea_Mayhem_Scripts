#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <chrono>

int main() {
    uintptr_t min_addr = 0x10000;
    uintptr_t max_addr = 0x7FFFFFFEFFFF;

    auto t1 = std::chrono::high_resolution_clock::now();
    uintptr_t address = min_addr;
    MEMORY_BASIC_INFORMATION mbi = {};
    int count = 0;

    while (address < max_addr && VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi))) {
        count++;
        address = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    auto t2 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t2 - t1).count();

    printf("Scanned %d memory regions in %.2f ms\n", count, ms);
    return 0;
}
