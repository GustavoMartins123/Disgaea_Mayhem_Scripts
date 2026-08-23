#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <cstdio>

#include "mod_loader_internal.h"

int wmain(int argument_count, wchar_t** arguments) {
    if (argument_count != 2) {
        std::fprintf(stderr, "uso: mod_loader_validate.exe <diretorio-do-jogo>\n");
        return 64;
    }
    const DWORD result = DmModLoaderValidate(arguments[1]);
    if (result != 0) {
        std::fprintf(stderr, "validacao do Mod Loader falhou (codigo=%lu)\n", result);
        return static_cast<int>(result);
    }
    std::printf("Mod Loader ABI v1: manifestos e plugins validos.\n");
    return 0;
}
