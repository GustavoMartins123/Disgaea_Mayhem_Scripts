#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <dxgi1_4.h>

#include "mod_loader_internal.h"

namespace {

INIT_ONCE g_dxgi_once = INIT_ONCE_STATIC_INIT;
INIT_ONCE g_loader_once = INIT_ONCE_STATIC_INIT;
HMODULE g_real_dxgi = nullptr;

BOOL CALLBACK LoadSystemDxgi(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t system_directory[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(system_directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH - 10) return FALSE;
    if (lstrcatW(system_directory, L"\\dxgi.dll") == nullptr) return FALSE;
    g_real_dxgi = LoadLibraryW(system_directory);
    return g_real_dxgi != nullptr;
}

BOOL CALLBACK StartModLoader(PINIT_ONCE, PVOID, PVOID*) {
    HANDLE thread = CreateThread(nullptr, 0, &DmModLoaderRun, nullptr, 0, nullptr);
    if (thread == nullptr) return FALSE;
    CloseHandle(thread);
    return TRUE;
}

template <typename T>
T GetRealProc(const char* name) {
    InitOnceExecuteOnce(&g_loader_once, &StartModLoader, nullptr, nullptr);
    if (!InitOnceExecuteOnce(&g_dxgi_once, &LoadSystemDxgi, nullptr, nullptr) || g_real_dxgi == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(g_real_dxgi, name)));
}

}  // namespace

#define DXGI_EXPORT extern "C" __declspec(dllexport)

DXGI_EXPORT HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** factory) {
    static const auto function = GetRealProc<HRESULT(WINAPI*)(REFIID, void**)>("CreateDXGIFactory");
    return function != nullptr ? function(riid, factory) : E_FAIL;
}

DXGI_EXPORT HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** factory) {
    static const auto function = GetRealProc<HRESULT(WINAPI*)(REFIID, void**)>("CreateDXGIFactory1");
    return function != nullptr ? function(riid, factory) : E_FAIL;
}

DXGI_EXPORT HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID riid, void** factory) {
    static const auto function = GetRealProc<HRESULT(WINAPI*)(UINT, REFIID, void**)>("CreateDXGIFactory2");
    return function != nullptr ? function(flags, riid, factory) : E_FAIL;
}

DXGI_EXPORT HRESULT WINAPI DXGIGetDebugInterface1(UINT flags, REFIID riid, void** debug) {
    static const auto function = GetRealProc<HRESULT(WINAPI*)(UINT, REFIID, void**)>("DXGIGetDebugInterface1");
    return function != nullptr ? function(flags, riid, debug) : E_FAIL;
}

DXGI_EXPORT HRESULT WINAPI DXGIDeclareAdapterRemovalSupport() {
    static const auto function = GetRealProc<HRESULT(WINAPI*)()>("DXGIDeclareAdapterRemovalSupport");
    return function != nullptr ? function() : E_FAIL;
}

DXGI_EXPORT HRESULT WINAPI DXGIDisableVBlankVirtualization() {
    static const auto function = GetRealProc<HRESULT(WINAPI*)()>("DXGIDisableVBlankVirtualization");
    return function != nullptr ? function() : E_FAIL;
}

DXGI_EXPORT void WINAPI ApplyCompatResolutionQuirking(void* first, void* second) {
    static const auto function = GetRealProc<void(WINAPI*)(void*, void*)>("ApplyCompatResolutionQuirking");
    if (function != nullptr) function(first, second);
}

DXGI_EXPORT void WINAPI CompatString(void* value) {
    static const auto function = GetRealProc<void(WINAPI*)(void*)>("CompatString");
    if (function != nullptr) function(value);
}

DXGI_EXPORT void WINAPI CompatValue(void* first, void* second) {
    static const auto function = GetRealProc<void(WINAPI*)(void*, void*)>("CompatValue");
    if (function != nullptr) function(first, second);
}

DXGI_EXPORT HRESULT WINAPI DXGID3D10CreateDevice(
    void* first, void* second, void* third, void* fourth, void* fifth, void* sixth) {
    static const auto function = GetRealProc<HRESULT(WINAPI*)(void*, void*, void*, void*, void*, void*)>(
        "DXGID3D10CreateDevice");
    return function != nullptr ? function(first, second, third, fourth, fifth, sixth) : E_FAIL;
}

DXGI_EXPORT HRESULT WINAPI DXGID3D10CreateLayeredDevice(
    void* first, void* second, void* third, void* fourth, void* fifth) {
    static const auto function = GetRealProc<HRESULT(WINAPI*)(void*, void*, void*, void*, void*)>(
        "DXGID3D10CreateLayeredDevice");
    return function != nullptr ? function(first, second, third, fourth, fifth) : E_FAIL;
}

DXGI_EXPORT SIZE_T WINAPI DXGID3D10GetLayeredDeviceSize(void* first, void* second) {
    static const auto function = GetRealProc<SIZE_T(WINAPI*)(void*, void*)>("DXGID3D10GetLayeredDeviceSize");
    return function != nullptr ? function(first, second) : 0;
}

DXGI_EXPORT HRESULT WINAPI DXGID3D10RegisterLayers(void* first, void* second) {
    static const auto function = GetRealProc<HRESULT(WINAPI*)(void*, void*)>("DXGID3D10RegisterLayers");
    return function != nullptr ? function(first, second) : E_FAIL;
}

DXGI_EXPORT void WINAPI DXGIDumpJournal(void* value) {
    static const auto function = GetRealProc<void(WINAPI*)(void*)>("DXGIDumpJournal");
    if (function != nullptr) function(value);
}

DXGI_EXPORT HRESULT WINAPI DXGIReportAdapterConfiguration(void* value) {
    static const auto function = GetRealProc<HRESULT(WINAPI*)(void*)>("DXGIReportAdapterConfiguration");
    return function != nullptr ? function(value) : E_FAIL;
}

DXGI_EXPORT HRESULT WINAPI PIXBeginCapture(void* first, void* second) {
    static const auto function = GetRealProc<HRESULT(WINAPI*)(void*, void*)>("PIXBeginCapture");
    return function != nullptr ? function(first, second) : E_FAIL;
}

DXGI_EXPORT HRESULT WINAPI PIXEndCapture(void* value) {
    static const auto function = GetRealProc<HRESULT(WINAPI*)(void*)>("PIXEndCapture");
    return function != nullptr ? function(value) : E_FAIL;
}

DXGI_EXPORT DWORD WINAPI PIXGetCaptureState() {
    static const auto function = GetRealProc<DWORD(WINAPI*)()>("PIXGetCaptureState");
    return function != nullptr ? function() : 0;
}

DXGI_EXPORT void WINAPI SetAppCompatStringPointer(void* first, void* second) {
    static const auto function = GetRealProc<void(WINAPI*)(void*, void*)>("SetAppCompatStringPointer");
    if (function != nullptr) function(first, second);
}

DXGI_EXPORT void WINAPI UpdateHMDEmulationStatus(void* value) {
    static const auto function = GetRealProc<void(WINAPI*)(void*)>("UpdateHMDEmulationStatus");
    if (function != nullptr) function(value);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
