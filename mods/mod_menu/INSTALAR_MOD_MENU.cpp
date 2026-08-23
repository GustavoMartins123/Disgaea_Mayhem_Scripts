#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>

typedef int (*pfn_LZ4_decompress_safe)(const char* src, char* dst, int compressedSize, int dstCapacity);
typedef int (*pfn_LZ4_compress_default)(const char* src, char* dst, int srcSize, int dstCapacity);

bool is_game_running() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32 pe = {};
    pe.dwSize = sizeof(pe);
    bool running = false;
    if (Process32First(hSnap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "Disgaea_Mayhem.exe") == 0) {
                running = true;
                break;
            }
        } while (Process32Next(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return running;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    printf("============================================================\n");
    printf("  DISGAEA MAYHEM - INSTALADOR DE ATLAS MOD MENU (C++ NATIVE)\n");
    printf("============================================================\n");

    if (is_game_running()) {
        printf("[ERRO] O jogo esta aberto! Feche o Disgaea Mayhem antes de instalar.\n");
        return 1;
    }

    char installer_path[MAX_PATH] = {};
    if (!GetModuleFileNameA(NULL, installer_path, MAX_PATH)) {
        printf("[ERRO] Nao foi possivel localizar o proprio instalador.\n");
        return 1;
    }
    char* separator = strrchr(installer_path, '\\');
    if (separator == NULL) {
        printf("[ERRO] Caminho do instalador invalido.\n");
        return 1;
    }
    *separator = '\0';

    char mod_directory[MAX_PATH] = {};
    snprintf(mod_directory, sizeof(mod_directory), "%s", installer_path);
    char relative_game_directory[MAX_PATH] = {};
    snprintf(relative_game_directory, sizeof(relative_game_directory), "%s\\..\\..", mod_directory);
    char game_directory[MAX_PATH] = {};
    if (!GetFullPathNameA(relative_game_directory, MAX_PATH, game_directory, NULL)) {
        printf("[ERRO] Nao foi possivel resolver a pasta do jogo.\n");
        return 1;
    }

    char target_fad[MAX_PATH] = {};
    snprintf(target_fad, sizeof(target_fad), "%s\\data\\fairy\\AnmDat_1_00_EN.fad", game_directory);
    DWORD fad_attributes = GetFileAttributesA(target_fad);
    if (fad_attributes == INVALID_FILE_ATTRIBUTES || (fad_attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        printf("[ERRO] Arquivo AnmDat_1_00_EN.fad nao encontrado.\n");
        return 1;
    }

    printf("[OK] Atlas localizado: %s\n", target_fad);

    char lz4_path[MAX_PATH] = {};
    snprintf(lz4_path, sizeof(lz4_path), "%s\\lz4.dll", game_directory);
    HMODULE hLz4 = LoadLibraryA(lz4_path);

    if (!hLz4) {
        printf("[ERRO] lz4.dll nao encontrada.\n");
        return 1;
    }

    pfn_LZ4_decompress_safe fn_decompress = reinterpret_cast<pfn_LZ4_decompress_safe>(
        reinterpret_cast<void*>(GetProcAddress(hLz4, "LZ4_decompress_safe")));
    pfn_LZ4_compress_default fn_compress = reinterpret_cast<pfn_LZ4_compress_default>(
        reinterpret_cast<void*>(GetProcAddress(hLz4, "LZ4_compress_default")));

    if (!fn_decompress || !fn_compress) {
        printf("[ERRO] Simbolos LZ4 nao encontrados na lz4.dll.\n");
        FreeLibrary(hLz4);
        return 1;
    }

    printf("[OK] Biblioteca LZ4 carregada com sucesso.\n");

    char target_dds[MAX_PATH] = {};
    snprintf(target_dds, sizeof(target_dds), "%s\\main_menu\\mods_slot.dds", mod_directory);
    DWORD dds_attributes = GetFileAttributesA(target_dds);
    if (dds_attributes == INVALID_FILE_ATTRIBUTES || (dds_attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        printf("[ERRO] mods_slot.dds nao encontrado.\n");
        FreeLibrary(hLz4);
        return 1;
    }

    FILE* f_dds = fopen(target_dds, "rb");
    if (!f_dds) {
        printf("[ERRO] Falha ao abrir %s\n", target_dds);
        FreeLibrary(hLz4);
        return 1;
    }
    fseek(f_dds, 0, SEEK_END);
    long dds_sz = ftell(f_dds);
    fseek(f_dds, 0, SEEK_SET);
    std::vector<uint8_t> dds_data(dds_sz);
    fread(dds_data.data(), 1, dds_sz, f_dds);
    fclose(f_dds);

    if (dds_sz <= 148) {
        printf("[ERRO] DDS invalido ou corrompido.\n");
        FreeLibrary(hLz4);
        return 1;
    }

    printf("[ERRO] Pacote validado, mas a gravacao NMPLTEX/YKCMP nao esta implementada neste instalador.\n");
    printf("       Nenhum arquivo foi alterado.\n");
    FreeLibrary(hLz4);
    return 2;
}
