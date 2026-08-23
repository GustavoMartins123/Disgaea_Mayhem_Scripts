#pragma once

#include <windows.h>

DWORD WINAPI DmModLoaderRun(LPVOID);
DWORD DmModLoaderValidate(const wchar_t* game_directory);
