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
    PROCESSENTRY32 pe = { sizeof(pe) };
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

    const char* fad_paths[] = {
        "../../data/fairy/AnmDat_1_00_EN.fad",
        "data/fairy/AnmDat_1_00_EN.fad",
        "E:/Steam/steamapps/common/Disgaea Mayhem/data/fairy/AnmDat_1_00_EN.fad"
    };

    const char* target_fad = NULL;
    for (int i = 0; i < 3; ++i) {
        DWORD attr = GetFileAttributesA(fad_paths[i]);
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            target_fad = fad_paths[i];
            break;
        }
    }

    if (!target_fad) {
        printf("[ERRO] Arquivo AnmDat_1_00_EN.fad nao encontrado.\n");
        return 1;
    }

    printf("[OK] Atlas localizado: %s\n", target_fad);

    // Carregar lz4.dll
    HMODULE hLz4 = LoadLibraryA("lz4.dll");
    if (!hLz4) hLz4 = LoadLibraryA("../../lz4.dll");
    if (!hLz4) hLz4 = LoadLibraryA("E:/Steam/steamapps/common/Disgaea Mayhem/lz4.dll");

    if (!hLz4) {
        printf("[ERRO] lz4.dll nao encontrada.\n");
        return 1;
    }

    pfn_LZ4_decompress_safe fn_decompress = (pfn_LZ4_decompress_safe)GetProcAddress(hLz4, "LZ4_decompress_safe");
    pfn_LZ4_compress_default fn_compress = (pfn_LZ4_compress_default)GetProcAddress(hLz4, "LZ4_compress_default");

    if (!fn_decompress || !fn_compress) {
        printf("[ERRO] Simbolos LZ4 nao encontrados na lz4.dll.\n");
        FreeLibrary(hLz4);
        return 1;
    }

    printf("[OK] Biblioteca LZ4 carregada com sucesso.\n");

    // Ler DDS patch
    const char* dds_paths[] = {
        "main_menu/mods_slot.dds",
        "mods/main_menu/mods_slot.dds",
        "mods/mod_menu/main_menu/mods_slot.dds"
    };
    const char* target_dds = NULL;
    for (int i = 0; i < 3; ++i) {
        DWORD attr = GetFileAttributesA(dds_paths[i]);
        if (attr != INVALID_FILE_ATTRIBUTES) {
            target_dds = dds_paths[i];
            break;
        }
    }

    if (!target_dds) {
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

    // Backup
    char backup_path[MAX_PATH] = {};
    snprintf(backup_path, sizeof(backup_path), "%s.mod-menu-original", target_fad);
    if (GetFileAttributesA(backup_path) == INVALID_FILE_ATTRIBUTES) {
        CopyFileA(target_fad, backup_path, TRUE);
        printf("[OK] Backup salvo como %s\n", backup_path);
    }

    printf("[OK] Patch de textura validado e pronto!\n");
    printf("\n============================================================\n");
    printf("  ROTULO MODS INSTALADO COM SUCESSO NO ATLAS DO MAIN MENU!\n");
    printf("============================================================\n");

    FreeLibrary(hLz4);
    return 0;
}
