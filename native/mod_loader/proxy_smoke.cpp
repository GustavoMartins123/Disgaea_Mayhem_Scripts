#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <dxgi1_4.h>

#include <cstdio>

using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);

int main() {
    HMODULE proxy = LoadLibraryW(L"dxgi.dll");
    if (proxy == nullptr) {
        std::fprintf(stderr, "LoadLibrary(dxgi.dll) falhou: %lu\n", GetLastError());
        return 10;
    }
    const auto create_factory = reinterpret_cast<CreateFactoryFn>(
        reinterpret_cast<void*>(GetProcAddress(proxy, "CreateDXGIFactory1")));
    if (create_factory == nullptr) return 11;

    IDXGIFactory1* factory = nullptr;
    const HRESULT result = create_factory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (FAILED(result) || factory == nullptr) {
        std::fprintf(stderr, "CreateDXGIFactory1 falhou: 0x%08lX\n", static_cast<unsigned long>(result));
        return 12;
    }
    factory->Release();
    Sleep(3000);
    return 0;
}
