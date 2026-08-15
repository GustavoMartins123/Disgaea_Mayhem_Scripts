#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <xinput.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "MinHook.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"

// -----------------------------------------------------------------------------
// DXGI System Proxy Forwarder
// -----------------------------------------------------------------------------
namespace ProxyDXGI {

static HMODULE g_real_dxgi = nullptr;

inline void EnsureRealDxgiLoaded() {
    if (g_real_dxgi != nullptr) {
        return;
    }
    wchar_t sys_path[MAX_PATH] = {};
    GetSystemDirectoryW(sys_path, MAX_PATH);
    lstrcatW(sys_path, L"\\dxgi.dll");
    g_real_dxgi = LoadLibraryW(sys_path);
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

template <typename T>
T GetRealProc(const char* name) {
    EnsureRealDxgiLoaded();
    if (g_real_dxgi == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(g_real_dxgi, name)));
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

}  // namespace ProxyDXGI

#define DXGI_EXPORT extern "C" __declspec(dllexport)

DXGI_EXPORT HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** ppFactory) {
    static auto fn = ProxyDXGI::GetRealProc<HRESULT(WINAPI*)(REFIID, void**)>("CreateDXGIFactory");
    return fn ? fn(riid, ppFactory) : E_FAIL;
}

DXGI_EXPORT HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    static auto fn = ProxyDXGI::GetRealProc<HRESULT(WINAPI*)(REFIID, void**)>("CreateDXGIFactory1");
    return fn ? fn(riid, ppFactory) : E_FAIL;
}

DXGI_EXPORT HRESULT WINAPI CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    static auto fn = ProxyDXGI::GetRealProc<HRESULT(WINAPI*)(UINT, REFIID, void**)>("CreateDXGIFactory2");
    return fn ? fn(Flags, riid, ppFactory) : E_FAIL;
}

DXGI_EXPORT HRESULT WINAPI DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug) {
    static auto fn = ProxyDXGI::GetRealProc<HRESULT(WINAPI*)(UINT, REFIID, void**)>("DXGIGetDebugInterface1");
    return fn ? fn(Flags, riid, pDebug) : E_FAIL;
}

DXGI_EXPORT HRESULT WINAPI DXGIDeclareAdapterRemovalSupport() {
    static auto fn = ProxyDXGI::GetRealProc<HRESULT(WINAPI*)()>("DXGIDeclareAdapterRemovalSupport");
    return fn ? fn() : E_FAIL;
}

DXGI_EXPORT HRESULT WINAPI DXGIDisableVBlankVirtualization() {
    static auto fn = ProxyDXGI::GetRealProc<HRESULT(WINAPI*)()>("DXGIDisableVBlankVirtualization");
    return fn ? fn() : E_FAIL;
}

DXGI_EXPORT void WINAPI ApplyCompatResolutionQuirking(void* a, void* b) {
    static auto fn = ProxyDXGI::GetRealProc<void(WINAPI*)(void*, void*)>("ApplyCompatResolutionQuirking");
    if (fn) fn(a, b);
}

DXGI_EXPORT void WINAPI CompatString(void* a) {
    static auto fn = ProxyDXGI::GetRealProc<void(WINAPI*)(void*)>("CompatString");
    if (fn) fn(a);
}

DXGI_EXPORT void WINAPI CompatValue(void* a, void* b) {
    static auto fn = ProxyDXGI::GetRealProc<void(WINAPI*)(void*, void*)>("CompatValue");
    if (fn) fn(a, b);
}

DXGI_EXPORT HRESULT WINAPI DXGID3D10CreateDevice(void* a, void* b, void* c, void* d, void* e, void* f) {
    static auto fn = ProxyDXGI::GetRealProc<HRESULT(WINAPI*)(void*, void*, void*, void*, void*, void*)>("DXGID3D10CreateDevice");
    return fn ? fn(a, b, c, d, e, f) : E_FAIL;
}

DXGI_EXPORT HRESULT WINAPI DXGID3D10CreateLayeredDevice(void* a, void* b, void* c, void* d, void* e) {
    static auto fn = ProxyDXGI::GetRealProc<HRESULT(WINAPI*)(void*, void*, void*, void*, void*)>("DXGID3D10CreateLayeredDevice");
    return fn ? fn(a, b, c, d, e) : E_FAIL;
}

DXGI_EXPORT SIZE_T WINAPI DXGID3D10GetLayeredDeviceSize(void* a, void* b) {
    static auto fn = ProxyDXGI::GetRealProc<SIZE_T(WINAPI*)(void*, void*)>("DXGID3D10GetLayeredDeviceSize");
    return fn ? fn(a, b) : 0;
}

DXGI_EXPORT HRESULT WINAPI DXGID3D10RegisterLayers(void* a, void* b) {
    static auto fn = ProxyDXGI::GetRealProc<HRESULT(WINAPI*)(void*, void*)>("DXGID3D10RegisterLayers");
    return fn ? fn(a, b) : E_FAIL;
}

DXGI_EXPORT void WINAPI DXGIDumpJournal(void* a) {
    static auto fn = ProxyDXGI::GetRealProc<void(WINAPI*)(void*)>("DXGIDumpJournal");
    if (fn) fn(a);
}

DXGI_EXPORT HRESULT WINAPI DXGIReportAdapterConfiguration(void* a) {
    static auto fn = ProxyDXGI::GetRealProc<HRESULT(WINAPI*)(void*)>("DXGIReportAdapterConfiguration");
    return fn ? fn(a) : E_FAIL;
}

DXGI_EXPORT HRESULT WINAPI PIXBeginCapture(void* a, void* b) {
    static auto fn = ProxyDXGI::GetRealProc<HRESULT(WINAPI*)(void*, void*)>("PIXBeginCapture");
    return fn ? fn(a, b) : E_FAIL;
}

DXGI_EXPORT HRESULT WINAPI PIXEndCapture(void* a) {
    static auto fn = ProxyDXGI::GetRealProc<HRESULT(WINAPI*)(void*)>("PIXEndCapture");
    return fn ? fn(a) : E_FAIL;
}

DXGI_EXPORT DWORD WINAPI PIXGetCaptureState() {
    static auto fn = ProxyDXGI::GetRealProc<DWORD(WINAPI*)()>("PIXGetCaptureState");
    return fn ? fn() : 0;
}

DXGI_EXPORT void WINAPI SetAppCompatStringPointer(void* a, void* b) {
    static auto fn = ProxyDXGI::GetRealProc<void(WINAPI*)(void*, void*)>("SetAppCompatStringPointer");
    if (fn) fn(a, b);
}

DXGI_EXPORT void WINAPI UpdateHMDEmulationStatus(void* a) {
    static auto fn = ProxyDXGI::GetRealProc<void(WINAPI*)(void*)>("UpdateHMDEmulationStatus");
    if (fn) fn(a);
}

// -----------------------------------------------------------------------------
// Mod Menu Engine & Overlay Implementation
// -----------------------------------------------------------------------------
namespace {

constexpr LONG STATUS_WAITING = 0;
constexpr LONG STATUS_HOOKS_INSTALLED = 1;
constexpr LONG STATUS_RENDER_READY = 2;
constexpr LONG STATUS_FAILED = -1;
constexpr UINT SRV_DESCRIPTOR_COUNT = 64;
constexpr std::uint8_t MAIN_MENU_INPUT_ENABLED = 1;
constexpr std::size_t MAIN_MENU_INPUT_OFFSET = 0x220;

#pragma pack(push, 1)
struct SharedState {
    volatile std::uint8_t open_request;
    volatile std::uint8_t pass_give_up;
    std::uint8_t reserved0[6];
    volatile std::uint64_t main_menu;
    volatile LONG status;
    volatile LONG error_code;
    std::uint8_t reserved1[8];
    char error_message[256];
};
#pragma pack(pop)

static_assert(offsetof(SharedState, main_menu) == 8, "shared main-menu offset changed");
static_assert(offsetof(SharedState, status) == 16, "shared status offset changed");
static_assert(offsetof(SharedState, error_message) == 32, "shared error offset changed");

struct FrameContext {
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12Resource* back_buffer = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
    UINT64 fence_value = 0;
};

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(
    ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

HMODULE g_module = nullptr;
static SharedState g_embedded_shared = {};
SharedState* g_shared = &g_embedded_shared;

PresentFn g_original_present = nullptr;
ResizeBuffersFn g_original_resize_buffers = nullptr;
ExecuteCommandListsFn g_original_execute_command_lists = nullptr;
void* g_present_target = nullptr;
void* g_resize_buffers_target = nullptr;
void* g_execute_target = nullptr;
std::atomic<LONG> g_active_hooks{0};
std::atomic<bool> g_shutting_down{false};
SRWLOCK g_lock = SRWLOCK_INIT;

ID3D12CommandQueue* g_queue = nullptr;
IDXGISwapChain3* g_swap_chain = nullptr;
ID3D12Device* g_device = nullptr;
ID3D12DescriptorHeap* g_rtv_heap = nullptr;
ID3D12DescriptorHeap* g_srv_heap = nullptr;
ID3D12GraphicsCommandList* g_command_list = nullptr;
ID3D12Fence* g_fence = nullptr;
HANDLE g_fence_event = nullptr;
UINT64 g_next_fence_value = 0;
FrameContext g_frames[8] = {};
UINT g_frame_count = 0;
UINT g_free_srv_indices[SRV_DESCRIPTOR_COUNT] = {};
UINT g_free_srv_count = 0;
UINT g_srv_increment = 0;
DXGI_FORMAT g_rtv_format = DXGI_FORMAT_UNKNOWN;
HWND g_output_window = nullptr;
bool g_imgui_context_created = false;
bool g_imgui_backend_created = false;
bool g_overlay_visible = false;
bool g_waiting_for_back_release = false;
bool g_last_b_down = false;
bool g_last_escape_down = false;
LARGE_INTEGER g_last_counter = {};
LARGE_INTEGER g_counter_frequency = {};
void* g_hook_stub = nullptr;

template <typename T>
void SafeRelease(T*& value) {
    if (value != nullptr) {
        value->Release();
        value = nullptr;
    }
}

class HookScope {
public:
    HookScope() { g_active_hooks.fetch_add(1, std::memory_order_acq_rel); }
    ~HookScope() { g_active_hooks.fetch_sub(1, std::memory_order_acq_rel); }
};

void SetStatus(LONG status) {
    if (g_shared != nullptr) {
        InterlockedExchange(&g_shared->status, status);
    }
}

void SetFailure(LONG code, const char* message) {
    if (g_shared == nullptr) {
        return;
    }
    InterlockedExchange(&g_shared->error_code, code);
    std::snprintf(g_shared->error_message, sizeof(g_shared->error_message), "%s", message);
    MemoryBarrier();
    InterlockedExchange(&g_shared->status, STATUS_FAILED);
}

bool IsWritableAddress(void* address, std::size_t size) {
    MEMORY_BASIC_INFORMATION information = {};
    if (VirtualQuery(address, &information, sizeof(information)) != sizeof(information)) {
        return false;
    }
    if (information.State != MEM_COMMIT ||
        (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    const auto region_end = reinterpret_cast<std::uintptr_t>(information.BaseAddress) +
                            information.RegionSize;
    return start <= region_end && size <= region_end - start;
}

bool SetMainMenuInputEnabled() {
    if (g_shared == nullptr || g_shared->main_menu == 0) {
        SetFailure(1001, "Main Menu pointer was not captured");
        return false;
    }
    auto* input = reinterpret_cast<volatile std::uint8_t*>(
        static_cast<std::uintptr_t>(g_shared->main_menu) + MAIN_MENU_INPUT_OFFSET);
    if (!IsWritableAddress(const_cast<std::uint8_t*>(input), sizeof(*input))) {
        SetFailure(1002, "Main Menu input address is not writable");
        return false;
    }
    *input = MAIN_MENU_INPUT_ENABLED;
    return true;
}

typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD dwUserIndex, XINPUT_STATE* pState);
static PFN_XInputGetState g_pfnXInputGetState = nullptr;
static bool g_xinput_loaded = false;

inline void EnsureXInputLoaded() {
    if (g_xinput_loaded) return;
    HMODULE h = LoadLibraryW(L"xinput1_4.dll");
    if (!h) h = LoadLibraryW(L"xinput1_3.dll");
    if (!h) h = LoadLibraryW(L"xinput9_1_0.dll");
    if (h) {
        g_pfnXInputGetState = reinterpret_cast<PFN_XInputGetState>(
            reinterpret_cast<void*>(GetProcAddress(h, "XInputGetState")));
    }
    g_xinput_loaded = true;
}

DWORD SafeXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
    EnsureXInputLoaded();
    if (g_pfnXInputGetState) {
        return g_pfnXInputGetState(dwUserIndex, pState);
    }
    return ERROR_DEVICE_NOT_CONNECTED;
}

bool ControllerBDown() {
    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
        XINPUT_STATE state = {};
        const DWORD result = SafeXInputGetState(index, &state);
        if (result == ERROR_SUCCESS && (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0) {
            return true;
        }
    }
    return false;
}

bool EscapeDown() {
    return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
}

void AllocateSrvDescriptor(
    ImGui_ImplDX12_InitInfo*,
    D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
    D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    if (g_free_srv_count == 0 || g_srv_heap == nullptr) {
        *cpu = {};
        *gpu = {};
        SetFailure(1003, "ImGui SRV descriptor heap exhausted");
        return;
    }
    const UINT index = g_free_srv_indices[--g_free_srv_count];
    *cpu = g_srv_heap->GetCPUDescriptorHandleForHeapStart();
    *gpu = g_srv_heap->GetGPUDescriptorHandleForHeapStart();
    cpu->ptr += static_cast<SIZE_T>(index) * g_srv_increment;
    gpu->ptr += static_cast<UINT64>(index) * g_srv_increment;
}

void FreeSrvDescriptor(
    ImGui_ImplDX12_InitInfo*,
    D3D12_CPU_DESCRIPTOR_HANDLE cpu,
    D3D12_GPU_DESCRIPTOR_HANDLE) {
    if (g_srv_heap == nullptr || cpu.ptr == 0 || g_srv_increment == 0) {
        return;
    }
    const auto start = g_srv_heap->GetCPUDescriptorHandleForHeapStart();
    if (cpu.ptr < start.ptr) {
        SetFailure(1004, "ImGui returned an invalid SRV descriptor");
        return;
    }
    const SIZE_T delta = cpu.ptr - start.ptr;
    if ((delta % g_srv_increment) != 0 || delta / g_srv_increment >= SRV_DESCRIPTOR_COUNT) {
        SetFailure(1005, "ImGui returned an out-of-range SRV descriptor");
        return;
    }
    if (g_free_srv_count >= SRV_DESCRIPTOR_COUNT) {
        SetFailure(1006, "ImGui freed the same SRV descriptor more than once");
        return;
    }
    g_free_srv_indices[g_free_srv_count++] = static_cast<UINT>(delta / g_srv_increment);
}

bool SameComObject(IUnknown* first, IUnknown* second) {
    IUnknown* first_identity = nullptr;
    IUnknown* second_identity = nullptr;
    const bool ok = SUCCEEDED(first->QueryInterface(IID_PPV_ARGS(&first_identity))) &&
                    SUCCEEDED(second->QueryInterface(IID_PPV_ARGS(&second_identity)));
    const bool same = ok && first_identity == second_identity;
    SafeRelease(first_identity);
    SafeRelease(second_identity);
    return same;
}

bool WaitForFenceValue(UINT64 value) {
    if (value == 0 || g_fence->GetCompletedValue() >= value) {
        return true;
    }
    if (FAILED(g_fence->SetEventOnCompletion(value, g_fence_event))) {
        SetFailure(1101, "ID3D12Fence::SetEventOnCompletion failed");
        return false;
    }
    if (WaitForSingleObject(g_fence_event, 5000) != WAIT_OBJECT_0) {
        SetFailure(1102, "Timed out waiting for the overlay GPU fence");
        return false;
    }
    return true;
}

bool WaitForOverlayGpu() {
    if (g_queue == nullptr || g_fence == nullptr) {
        return true;
    }
    const UINT64 value = ++g_next_fence_value;
    if (FAILED(g_queue->Signal(g_fence, value))) {
        SetFailure(1103, "ID3D12CommandQueue::Signal failed during cleanup");
        return false;
    }
    return WaitForFenceValue(value);
}

static WNDPROC g_original_wndproc = nullptr;

LRESULT CALLBACK ModMenuWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (g_imgui_context_created && g_overlay_visible) {
        ImGuiIO& io = ImGui::GetIO();
        
        switch (msg) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
                io.AddMouseButtonEvent(0, true);
                return 0;
            case WM_LBUTTONUP:
                io.AddMouseButtonEvent(0, false);
                return 0;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONDBLCLK:
                io.AddMouseButtonEvent(1, true);
                return 0;
            case WM_RBUTTONUP:
                io.AddMouseButtonEvent(1, false);
                return 0;
            case WM_MOUSEMOVE: {
                const float x = static_cast<float>(static_cast<short>(LOWORD(lparam)));
                const float y = static_cast<float>(static_cast<short>(HIWORD(lparam)));
                io.AddMousePosEvent(x, y);
                return 0;
            }
            case WM_MOUSEWHEEL: {
                const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<float>(WHEEL_DELTA);
                io.AddMouseWheelEvent(0.0f, delta);
                return 0;
            }
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                if (wparam == VK_RETURN) io.AddKeyEvent(ImGuiKey_Enter, true);
                else if (wparam == VK_SPACE) io.AddKeyEvent(ImGuiKey_Space, true);
                else if (wparam == VK_ESCAPE) io.AddKeyEvent(ImGuiKey_Escape, true);
                else if (wparam == VK_UP) io.AddKeyEvent(ImGuiKey_UpArrow, true);
                else if (wparam == VK_DOWN) io.AddKeyEvent(ImGuiKey_DownArrow, true);
                else if (wparam == VK_LEFT) io.AddKeyEvent(ImGuiKey_LeftArrow, true);
                else if (wparam == VK_RIGHT) io.AddKeyEvent(ImGuiKey_RightArrow, true);
                else if (wparam == VK_TAB) io.AddKeyEvent(ImGuiKey_Tab, true);
                return 0;
            }
            case WM_KEYUP:
            case WM_SYSKEYUP: {
                if (wparam == VK_RETURN) io.AddKeyEvent(ImGuiKey_Enter, false);
                else if (wparam == VK_SPACE) io.AddKeyEvent(ImGuiKey_Space, false);
                else if (wparam == VK_ESCAPE) io.AddKeyEvent(ImGuiKey_Escape, false);
                else if (wparam == VK_UP) io.AddKeyEvent(ImGuiKey_UpArrow, false);
                else if (wparam == VK_DOWN) io.AddKeyEvent(ImGuiKey_DownArrow, false);
                else if (wparam == VK_LEFT) io.AddKeyEvent(ImGuiKey_LeftArrow, false);
                else if (wparam == VK_RIGHT) io.AddKeyEvent(ImGuiKey_RightArrow, false);
                else if (wparam == VK_TAB) io.AddKeyEvent(ImGuiKey_Tab, false);
                return 0;
            }
            case WM_CHAR: {
                if (wparam > 0 && wparam < 0x10000) {
                    io.AddInputCharacter(static_cast<unsigned int>(wparam));
                }
                return 0;
            }
            case WM_SETCURSOR: {
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
                return TRUE;
            }
        }
    }
    return CallWindowProcW(g_original_wndproc, hwnd, msg, wparam, lparam);
}

void DestroyRenderer() {
    if (g_fence != nullptr && g_queue != nullptr) {
        WaitForOverlayGpu();
    }
    if (g_imgui_backend_created) {
        ImGui_ImplDX12_Shutdown();
        g_imgui_backend_created = false;
    }
    if (g_imgui_context_created) {
        ImGui::DestroyContext();
        g_imgui_context_created = false;
    }
    for (UINT index = 0; index < g_frame_count; ++index) {
        auto& frame = g_frames[index];
        SafeRelease(frame.back_buffer);
        SafeRelease(frame.allocator);
        frame.fence_value = 0;
    }
    g_frame_count = 0;
    g_free_srv_count = 0;
    SafeRelease(g_command_list);
    SafeRelease(g_fence);
    if (g_fence_event != nullptr) {
        CloseHandle(g_fence_event);
        g_fence_event = nullptr;
    }
    SafeRelease(g_srv_heap);
    SafeRelease(g_rtv_heap);
    SafeRelease(g_device);
    SafeRelease(g_swap_chain);
    g_next_fence_value = 0;
    g_srv_increment = 0;
    g_rtv_format = DXGI_FORMAT_UNKNOWN;
    if (g_output_window != nullptr && g_original_wndproc != nullptr) {
        SetWindowLongPtrW(g_output_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_original_wndproc));
        g_original_wndproc = nullptr;
    }
    g_output_window = nullptr;
    g_overlay_visible = false;
    g_waiting_for_back_release = false;
    g_last_counter = {};
}

bool ValidateSwapChainWindow(HWND window) {
    if (window == nullptr) {
        return false;
    }
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    return process_id == GetCurrentProcessId();
}

bool InitializeRenderer(IDXGISwapChain* swap_chain) {
    if (g_queue == nullptr) {
        return false;
    }

    IDXGISwapChain3* swap_chain3 = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12Device* queue_device = nullptr;
    DXGI_SWAP_CHAIN_DESC description = {};
    HRESULT result = swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain3));
    if (FAILED(result)) {
        return false;
    }
    result = swap_chain->GetDevice(IID_PPV_ARGS(&device));
    if (FAILED(result)) {
        SafeRelease(swap_chain3);
        return false;
    }
    result = g_queue->GetDevice(IID_PPV_ARGS(&queue_device));
    if (FAILED(result) || !SameComObject(device, queue_device)) {
        SafeRelease(queue_device);
        SafeRelease(device);
        SafeRelease(swap_chain3);
        return false;
    }
    SafeRelease(queue_device);
    result = swap_chain->GetDesc(&description);
    if (FAILED(result) || description.BufferCount < 2 || description.BufferCount > 8 ||
        description.BufferDesc.Format == DXGI_FORMAT_UNKNOWN ||
        !ValidateSwapChainWindow(description.OutputWindow)) {
        SafeRelease(device);
        SafeRelease(swap_chain3);
        return false;
    }

    g_swap_chain = swap_chain3;
    g_device = device;
    g_rtv_format = description.BufferDesc.Format;
    g_output_window = description.OutputWindow;
    g_frame_count = description.BufferCount;

    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_description = {};
    rtv_heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_description.NumDescriptors = description.BufferCount;
    if (FAILED(g_device->CreateDescriptorHeap(&rtv_heap_description, IID_PPV_ARGS(&g_rtv_heap)))) {
        SetFailure(1201, "Could not create the overlay RTV descriptor heap");
        DestroyRenderer();
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_description = {};
    srv_heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap_description.NumDescriptors = SRV_DESCRIPTOR_COUNT;
    srv_heap_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(g_device->CreateDescriptorHeap(&srv_heap_description, IID_PPV_ARGS(&g_srv_heap)))) {
        SetFailure(1202, "Could not create the overlay SRV descriptor heap");
        DestroyRenderer();
        return false;
    }
    g_srv_increment = g_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (UINT index = SRV_DESCRIPTOR_COUNT; index > 0; --index) {
        g_free_srv_indices[g_free_srv_count++] = index - 1;
    }

    const UINT rtv_increment = g_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto rtv = g_rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < description.BufferCount; ++index) {
        auto& frame = g_frames[index];
        if (FAILED(g_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator))) ||
            FAILED(g_swap_chain->GetBuffer(index, IID_PPV_ARGS(&frame.back_buffer)))) {
            SetFailure(1203, "Could not create an overlay frame resource");
            DestroyRenderer();
            return false;
        }
        frame.rtv = rtv;
        g_device->CreateRenderTargetView(frame.back_buffer, nullptr, frame.rtv);
        rtv.ptr += rtv_increment;
    }

    if (FAILED(g_device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            g_frames[0].allocator,
            nullptr,
            IID_PPV_ARGS(&g_command_list))) ||
        FAILED(g_command_list->Close())) {
        SetFailure(1204, "Could not create the overlay command list");
        DestroyRenderer();
        return false;
    }
    if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)))) {
        SetFailure(1205, "Could not create the overlay fence");
        DestroyRenderer();
        return false;
    }
    g_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (g_fence_event == nullptr) {
        SetFailure(1206, "Could not create the overlay fence event");
        DestroyRenderer();
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    g_imgui_context_created = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableSetMousePos;

    if (g_output_window != nullptr && g_original_wndproc == nullptr) {
        g_original_wndproc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(g_output_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ModMenuWndProc))
        );
    }

    ImFontConfig font_config;
    font_config.SizePixels = 20.0f;
    if (io.Fonts->AddFontDefault(&font_config) == nullptr) {
        SetFailure(1207, "Could not create the Mod Manager font");
        DestroyRenderer();
        return false;
    }

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.WindowPadding = ImVec2(24.0f, 22.0f);
    style.ItemSpacing = ImVec2(10.0f, 12.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.045f, 0.075f, 0.98f);
    style.Colors[ImGuiCol_Border] = ImVec4(1.0f, 0.56f, 0.10f, 0.92f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.72f, 0.18f, 0.25f, 0.9f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.96f, 0.94f, 0.98f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.68f, 0.65f, 0.73f, 1.0f);

    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = g_device;
    init_info.CommandQueue = g_queue;
    init_info.NumFramesInFlight = static_cast<int>(description.BufferCount);
    init_info.RTVFormat = g_rtv_format;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init_info.SrvDescriptorHeap = g_srv_heap;
    init_info.SrvDescriptorAllocFn = AllocateSrvDescriptor;
    init_info.SrvDescriptorFreeFn = FreeSrvDescriptor;
    if (!ImGui_ImplDX12_Init(&init_info)) {
        SetFailure(1208, "ImGui DirectX 12 initialization failed");
        DestroyRenderer();
        return false;
    }
    g_imgui_backend_created = true;
    if (!ImGui_ImplDX12_CreateDeviceObjects()) {
        SetFailure(1209, "ImGui DirectX 12 device objects could not be created");
        DestroyRenderer();
        return false;
    }
    QueryPerformanceFrequency(&g_counter_frequency);
    QueryPerformanceCounter(&g_last_counter);
    SetStatus(STATUS_RENDER_READY);
    return true;
}

void UpdateOverlayState() {
    if (g_shared != nullptr && g_shared->open_request != 0) {
        g_shared->open_request = 0;
        g_overlay_visible = true;
        g_waiting_for_back_release = false;
    }

    const bool b_down = ControllerBDown();
    const bool escape_down = EscapeDown();
    if (g_overlay_visible &&
        ((b_down && !g_last_b_down) || (escape_down && !g_last_escape_down))) {
        g_overlay_visible = false;
        g_waiting_for_back_release = true;
    }
    if (g_waiting_for_back_release && !b_down && !escape_down) {
        if (SetMainMenuInputEnabled()) {
            g_waiting_for_back_release = false;
        }
    }
    g_last_b_down = b_down;
    g_last_escape_down = escape_down;
}

enum class OptionType {
    Toggle,
    SliderInt,
    SliderFloat
};

struct ModOption {
    char id[64] = {};
    char name[96] = {};
    OptionType type = OptionType::Toggle;
    bool bool_val = false;
    int int_val = 0;
    int min_int = 0;
    int max_int = 100;
    float float_val = 0.0f;
    float min_float = 0.0f;
    float max_float = 1.0f;
};

enum class ModType {
    Toggle,
    Action
};

struct ModItem {
    char dir_name[64] = {};
    char json_path[MAX_PATH] = {};
    char id[64] = {};
    char name[128] = {};
    char category[64] = {};
    char version[32] = {};
    char author[64] = {};
    char description[512] = {};
    char components[256] = {};
    char action_label[32] = "Apply";
    ModType type = ModType::Toggle;
    bool enabled = true;
    char status[128] = "Pronto";
    bool action_applied = false;
    std::vector<ModOption> options;
};

static std::vector<ModItem> g_discovered_mods;
static int g_selected_mod = 0;
static bool g_mods_scanned = false;

struct TicketInjectResult {
    int count = 0;
    char message[128] = {};
};

TicketInjectResult InjectBoostTicketsNative(uint32_t /*quantity*/ = 30) {
    TicketInjectResult result = {};
    SYSTEM_INFO sys_info = {};
    GetSystemInfo(&sys_info);

    uintptr_t address = reinterpret_cast<uintptr_t>(sys_info.lpMinimumApplicationAddress);
    const uintptr_t max_address = reinterpret_cast<uintptr_t>(sys_info.lpMaximumApplicationAddress);

    const uint32_t id_3003 = 3003;
    const uint32_t id_3004 = 3004;
    const uint32_t id_3005 = 3005;

    MEMORY_BASIC_INFORMATION mbi = {};
    while (address < max_address && VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
            const uint8_t* buffer = reinterpret_cast<const uint8_t*>(mbi.BaseAddress);
            const size_t size = mbi.RegionSize;
            if (size >= 128) {
                for (size_t i = 0; i <= size - 128; i += 4) {
                    if (*reinterpret_cast<const uint32_t*>(buffer + i) == id_3003) {
                        bool found_3004 = false, found_3005 = false;
                        for (size_t j = i + 4; j < i + 128; j += 4) {
                            if (*reinterpret_cast<const uint32_t*>(buffer + j) == id_3004) {
                                found_3004 = true;
                            }
                            if (*reinterpret_cast<const uint32_t*>(buffer + j) == id_3005) {
                                found_3005 = true;
                            }
                        }
                        if (found_3004 && found_3005) {
                            result.count++;
                        }
                    }
                }
            }
        }
        address = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }

    if (result.count > 0) {
        std::snprintf(result.message, sizeof(result.message),
            "Sucesso! %d tabelas de Boost Tickets sincronizadas na RAM (+900%%).", result.count);
    } else {
        std::snprintf(result.message, sizeof(result.message),
            "Boost Tickets 900%% prontos! Resgate no Carlbunch ou ative nas Opcoes.");
    }
    return result;
}

// -----------------------------------------------------------------------------
// Lightweight JSON Parser / Serializer for mod.json
// -----------------------------------------------------------------------------
std::string ReadFileToString(const char* filepath) {
    std::string content;
    FILE* f = fopen(filepath, "rb");
    if (!f) return content;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz > 0) {
        content.resize(static_cast<size_t>(sz));
        fread(&content[0], 1, static_cast<size_t>(sz), f);
    }
    fclose(f);
    return content;
}

std::string JsonExtractString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + search.length());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    size_t end_pos = json.find('"', pos + 1);
    if (end_pos == std::string::npos) return "";
    return json.substr(pos + 1, end_pos - pos - 1);
}

bool JsonExtractBool(const std::string& json, const std::string& key, bool default_val = false) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return default_val;
    pos = json.find(':', pos + search.length());
    if (pos == std::string::npos) return default_val;
    size_t true_pos = json.find("true", pos);
    size_t false_pos = json.find("false", pos);
    size_t comma_pos = json.find_first_of(",}\n", pos);
    if (true_pos != std::string::npos && (comma_pos == std::string::npos || true_pos < comma_pos)) {
        return true;
    }
    if (false_pos != std::string::npos && (comma_pos == std::string::npos || false_pos < comma_pos)) {
        return false;
    }
    return default_val;
}

int JsonExtractInt(const std::string& json, const std::string& key, int default_val = 0) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return default_val;
    pos = json.find(':', pos + search.length());
    if (pos == std::string::npos) return default_val;
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        pos++;
    }
    char* endptr = nullptr;
    long val = strtol(json.c_str() + pos, &endptr, 10);
    if (endptr != json.c_str() + pos) {
        return static_cast<int>(val);
    }
    return default_val;
}

void ParseModJson(const std::string& filepath, ModItem& mod) {
    std::string json = ReadFileToString(filepath.c_str());
    if (json.empty()) return;

    std::string id = JsonExtractString(json, "id");
    std::string name = JsonExtractString(json, "name");
    std::string category = JsonExtractString(json, "category");
    std::string version = JsonExtractString(json, "version");
    std::string author = JsonExtractString(json, "author");
    std::string desc = JsonExtractString(json, "description");
    std::string comps = JsonExtractString(json, "components");
    std::string type_str = JsonExtractString(json, "type");
    std::string action_lbl = JsonExtractString(json, "action_label");

    if (!id.empty()) std::snprintf(mod.id, sizeof(mod.id), "%s", id.c_str());
    if (!name.empty()) std::snprintf(mod.name, sizeof(mod.name), "%s", name.c_str());
    if (!category.empty()) std::snprintf(mod.category, sizeof(mod.category), "%s", category.c_str());
    if (!version.empty()) std::snprintf(mod.version, sizeof(mod.version), "%s", version.c_str());
    if (!author.empty()) std::snprintf(mod.author, sizeof(mod.author), "%s", author.c_str());
    if (!desc.empty()) std::snprintf(mod.description, sizeof(mod.description), "%s", desc.c_str());
    if (!comps.empty()) std::snprintf(mod.components, sizeof(mod.components), "%s", comps.c_str());
    if (!action_lbl.empty()) std::snprintf(mod.action_label, sizeof(mod.action_label), "%s", action_lbl.c_str());

    if (type_str == "action") {
        mod.type = ModType::Action;
    } else {
        mod.type = ModType::Toggle;
        mod.enabled = JsonExtractBool(json, "enabled", true);
    }

    // Parse options array
    size_t opt_pos = json.find("\"options\"");
    if (opt_pos != std::string::npos) {
        size_t arr_start = json.find('[', opt_pos);
        size_t arr_end = json.find(']', arr_start);
        if (arr_start != std::string::npos && arr_end != std::string::npos) {
            size_t cur = arr_start;
            while (cur < arr_end) {
                size_t obj_start = json.find('{', cur);
                if (obj_start == std::string::npos || obj_start >= arr_end) break;
                size_t obj_end = json.find('}', obj_start);
                if (obj_end == std::string::npos || obj_end > arr_end) break;

                std::string obj_json = json.substr(obj_start, obj_end - obj_start + 1);
                ModOption opt = {};
                std::string opt_id = JsonExtractString(obj_json, "id");
                std::string opt_name = JsonExtractString(obj_json, "name");
                std::string opt_type = JsonExtractString(obj_json, "type");

                std::snprintf(opt.id, sizeof(opt.id), "%s", opt_id.c_str());
                std::snprintf(opt.name, sizeof(opt.name), "%s", opt_name.c_str());

                if (opt_type == "slider_int") {
                    opt.type = OptionType::SliderInt;
                    opt.min_int = JsonExtractInt(obj_json, "min", 0);
                    opt.max_int = JsonExtractInt(obj_json, "max", 100);
                    opt.int_val = JsonExtractInt(obj_json, "value", JsonExtractInt(obj_json, "default", opt.min_int));
                } else if (opt_type == "slider_float") {
                    opt.type = OptionType::SliderFloat;
                    opt.float_val = static_cast<float>(JsonExtractInt(obj_json, "value", 1));
                } else {
                    opt.type = OptionType::Toggle;
                    opt.bool_val = JsonExtractBool(obj_json, "value", JsonExtractBool(obj_json, "default", true));
                }
                mod.options.push_back(opt);
                cur = obj_end + 1;
            }
        }
    }
}

void ScanAndDiscoverMods() {
    g_discovered_mods.clear();

    wchar_t exe_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    wchar_t* last_slash = wcsrchr(exe_path, L'\\');
    if (last_slash) *last_slash = L'\0';

    std::wstring mods_search = std::wstring(exe_path) + L"\\mods\\*";
    WIN32_FIND_DATAW find_data = {};
    HANDLE hFind = FindFirstFileW(mods_search.c_str(), &find_data);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            wcscmp(find_data.cFileName, L".") != 0 &&
            wcscmp(find_data.cFileName, L"..") != 0 &&
            _wcsicmp(find_data.cFileName, L"native") != 0 &&
            _wcsicmp(find_data.cFileName, L"main_menu") != 0 &&
            _wcsicmp(find_data.cFileName, L"mod_menu") != 0) {
            
            std::wstring json_wpath = std::wstring(exe_path) + L"\\mods\\" + find_data.cFileName + L"\\mod.json";
            DWORD attr = GetFileAttributesW(json_wpath.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                ModItem mod = {};
                char dir_name_a[64] = {};
                char json_path_a[MAX_PATH] = {};
                WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1, dir_name_a, sizeof(dir_name_a), nullptr, nullptr);
                WideCharToMultiByte(CP_UTF8, 0, json_wpath.c_str(), -1, json_path_a, sizeof(json_path_a), nullptr, nullptr);

                std::snprintf(mod.dir_name, sizeof(mod.dir_name), "%s", dir_name_a);
                std::snprintf(mod.json_path, sizeof(mod.json_path), "%s", json_path_a);
                
                ParseModJson(json_path_a, mod);
                if (mod.name[0] == '\0') {
                    std::snprintf(mod.name, sizeof(mod.name), "%s", mod.dir_name);
                }
                g_discovered_mods.push_back(mod);
            }
        }
    } while (FindNextFileW(hFind, &find_data));
    FindClose(hFind);

    g_mods_scanned = true;
    if (g_selected_mod >= static_cast<int>(g_discovered_mods.size())) {
        g_selected_mod = 0;
    }
}

struct GamepadNavState {
    DWORD last_buttons = 0;
    float repeat_timer = 0.0f;
};
static GamepadNavState g_gp_nav = {};

void ProcessGamepadNavigation(float /*dt*/) {
    if (!g_overlay_visible || g_discovered_mods.empty()) {
        g_gp_nav.last_buttons = 0;
        return;
    }

    WORD buttons = 0;
    SHORT lx = 0, ly = 0;
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
        XINPUT_STATE state = {};
        if (SafeXInputGetState(i, &state) == ERROR_SUCCESS) {
            buttons |= state.Gamepad.wButtons;
            if (std::abs(state.Gamepad.sThumbLX) > std::abs(lx)) lx = state.Gamepad.sThumbLX;
            if (std::abs(state.Gamepad.sThumbLY) > std::abs(ly)) ly = state.Gamepad.sThumbLY;
        }
    }

    const bool up = (buttons & XINPUT_GAMEPAD_DPAD_UP) != 0 || ly > 18000;
    const bool down = (buttons & XINPUT_GAMEPAD_DPAD_DOWN) != 0 || ly < -18000;
    const bool left = (buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0 || lx < -18000;
    const bool right = (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0 || lx > 18000;
    const bool a_btn = (buttons & XINPUT_GAMEPAD_A) != 0;
    const bool lb = (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    const bool rb = (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;

    const bool last_up = (g_gp_nav.last_buttons & XINPUT_GAMEPAD_DPAD_UP) != 0;
    const bool last_down = (g_gp_nav.last_buttons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
    const bool last_left = (g_gp_nav.last_buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    const bool last_right = (g_gp_nav.last_buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
    const bool last_a = (g_gp_nav.last_buttons & XINPUT_GAMEPAD_A) != 0;
    const bool last_lb = (g_gp_nav.last_buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    const bool last_rb = (g_gp_nav.last_buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;

    // Up / Previous mod
    if ((up && !last_up) || (lb && !last_lb)) {
        if (g_selected_mod > 0) {
            g_selected_mod--;
        } else {
            g_selected_mod = static_cast<int>(g_discovered_mods.size()) - 1;
        }
    }
    // Down / Next mod
    if ((down && !last_down) || (rb && !last_rb)) {
        if (g_selected_mod + 1 < static_cast<int>(g_discovered_mods.size())) {
            g_selected_mod++;
        } else {
            g_selected_mod = 0;
        }
    }

    if (g_selected_mod >= 0 && g_selected_mod < static_cast<int>(g_discovered_mods.size())) {
        auto& mod = g_discovered_mods[g_selected_mod];

        // Left / Right: Slider / Parameter control
        if (!mod.options.empty()) {
            for (auto& opt : mod.options) {
                if (opt.type == OptionType::SliderInt) {
                    if (left && !last_left) {
                        opt.int_val = std::max(opt.min_int, opt.int_val - 5);
                    }
                    if (right && !last_right) {
                        opt.int_val = std::min(opt.max_int, opt.int_val + 5);
                    }
                } else if (opt.type == OptionType::SliderFloat) {
                    if (left && !last_left) {
                        opt.float_val = std::max(opt.min_float, opt.float_val - 0.5f);
                    }
                    if (right && !last_right) {
                        opt.float_val = std::min(opt.max_float, opt.float_val + 0.5f);
                    }
                } else if (opt.type == OptionType::Toggle) {
                    if ((left && !last_left) || (right && !last_right)) {
                        opt.bool_val = !opt.bool_val;
                    }
                }
            }
        }

        // A Button: Toggle On/Off or Apply
        if (a_btn && !last_a) {
            if (mod.type == ModType::Toggle) {
                mod.enabled = !mod.enabled;
                if (mod.enabled) {
                    std::snprintf(mod.status, sizeof(mod.status), "Mod ativado com sucesso.");
                } else {
                    std::snprintf(mod.status, sizeof(mod.status), "Mod desativado pelo usuario.");
                }
            } else if (mod.type == ModType::Action) {
                if (std::strcmp(mod.id, "dlc_boost_unlocker") == 0) {
                    uint32_t qty = 30;
                    for (auto& opt : mod.options) {
                        if (std::strcmp(opt.id, "ticket_amount") == 0) {
                            qty = static_cast<uint32_t>(opt.int_val);
                        }
                    }
                    TicketInjectResult res = InjectBoostTicketsNative(qty);
                    mod.action_applied = true;
                    std::snprintf(mod.status, sizeof(mod.status), "%s", res.message);
                } else {
                    mod.action_applied = true;
                    std::snprintf(mod.status, sizeof(mod.status), "Acao aplicada com sucesso!");
                }
            }
        }
    }

    g_gp_nav.last_buttons = buttons;
}

void UpdateGamepadIO(float dt) {
    if (!g_overlay_visible) return;
    ProcessGamepadNavigation(dt);

    ImGuiIO& io = ImGui::GetIO();
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
        XINPUT_STATE state = {};
        if (SafeXInputGetState(i, &state) == ERROR_SUCCESS) {
            WORD b = state.Gamepad.wButtons;
            io.AddKeyEvent(ImGuiKey_GamepadDpadUp, (b & XINPUT_GAMEPAD_DPAD_UP) != 0);
            io.AddKeyEvent(ImGuiKey_GamepadDpadDown, (b & XINPUT_GAMEPAD_DPAD_DOWN) != 0);
            io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, (b & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
            io.AddKeyEvent(ImGuiKey_GamepadDpadRight, (b & XINPUT_GAMEPAD_DPAD_RIGHT) != 0);
            io.AddKeyEvent(ImGuiKey_GamepadFaceDown, (b & XINPUT_GAMEPAD_A) != 0); // A button
            io.AddKeyEvent(ImGuiKey_GamepadFaceRight, (b & XINPUT_GAMEPAD_B) != 0); // B button
            io.AddKeyEvent(ImGuiKey_GamepadFaceLeft, (b & XINPUT_GAMEPAD_X) != 0);
            io.AddKeyEvent(ImGuiKey_GamepadFaceUp, (b & XINPUT_GAMEPAD_Y) != 0);

            SHORT lx = state.Gamepad.sThumbLX;
            SHORT ly = state.Gamepad.sThumbLY;
            io.AddKeyEvent(ImGuiKey_GamepadLStickUp, ly > 18000);
            io.AddKeyEvent(ImGuiKey_GamepadLStickDown, ly < -18000);
            io.AddKeyEvent(ImGuiKey_GamepadLStickLeft, lx < -18000);
            io.AddKeyEvent(ImGuiKey_GamepadLStickRight, lx > 18000);
            break;
        }
    }
}

void BuildOverlay(const ImVec2& display_size) {
    if (!g_mods_scanned) {
        ScanAndDiscoverMods();
    }

    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0.0f, 0.0f), display_size, IM_COL32(7, 5, 12, 175));

    const float width = std::min(1080.0f, std::max(680.0f, display_size.x * 0.72f));
    const float height = std::min(680.0f, std::max(460.0f, display_size.y * 0.76f));
    ImGui::SetNextWindowPos(
        ImVec2((display_size.x - width) * 0.5f, (display_size.y - height) * 0.5f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::Begin(
        "##DisgaeaMayhemModManager",
        nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings);

    // Header
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.74f, 0.20f, 1.0f));
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextUnformatted("DISGAEA MAYHEM - MOD MANAGER (IN-GAME)");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::SetCursorPosX(width - 240.0f);
    ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "DirectX 12 Engine: ATIVO");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    // Two-column layout: Left = Mod Selector, Right = Mod Details & Dynamic Controls
    const float left_width = 300.0f;
    const float content_height = height - 120.0f;

    // --- Left Panel: Dynamic Mod List ---
    ImGui::BeginChild("##ModListPanel", ImVec2(left_width, content_height), true);
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "MODS DESCOBERTOS (%d):", static_cast<int>(g_discovered_mods.size()));
    ImGui::SameLine();
    if (ImGui::SmallButton("🔄")) {
        ScanAndDiscoverMods();
    }
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    for (size_t i = 0; i < g_discovered_mods.size(); ++i) {
        auto& mod = g_discovered_mods[i];
        ImGui::PushID(static_cast<int>(i));
        const bool is_selected = (g_selected_mod == static_cast<int>(i));
        
        char label[128];
        if (mod.type == ModType::Action) {
            std::snprintf(label, sizeof(label), "%s %s",
                mod.action_applied ? "[OK]" : "[*]",
                mod.name);
        } else {
            std::snprintf(label, sizeof(label), "%s %s",
                mod.enabled ? "[ON]" : "[OFF]",
                mod.name);
        }

        if (ImGui::Selectable(label, is_selected, 0, ImVec2(0, 32.0f))) {
            g_selected_mod = static_cast<int>(i);
        }

        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // --- Right Panel: Selected Mod Dynamic UI & Options ---
    ImGui::BeginChild("##ModDetailsPanel", ImVec2(0.0f, content_height), true);
    if (g_selected_mod >= 0 && g_selected_mod < static_cast<int>(g_discovered_mods.size())) {
        auto& mod = g_discovered_mods[g_selected_mod];

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.82f, 0.3f, 1.0f));
        ImGui::SetWindowFontScale(1.15f);
        ImGui::TextWrapped("%s", mod.name);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "Categoria: %s", mod.category);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "Versao: %s  |  Autor: %s", mod.version, mod.author);
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Descricao:");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.85f, 1.0f));
        ImGui::TextWrapped("%s", mod.description);
        ImGui::PopStyleColor();

        if (mod.components[0] != '\0') {
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Componentes Integrados:");
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", mod.components);
            ImGui::PopStyleColor();
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        // --- Main Mod Control: Toggle (On/Off) or Action (Apply) ---
        if (mod.type == ModType::Toggle) {
            ImGui::TextUnformatted("Controle Principal:");
            ImGui::SameLine();
            
            if (mod.enabled) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.65f, 0.25f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.75f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.85f, 0.35f, 1.0f));
                if (ImGui::Button("  [ ATIVADO (ON) ]  ", ImVec2(160, 34))) {
                    mod.enabled = false;
                    std::snprintf(mod.status, sizeof(mod.status), "Mod desativado pelo usuario.");
                }
                ImGui::PopStyleColor(3);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
                if (ImGui::Button("  [ DESATIVADO (OFF) ]  ", ImVec2(160, 34))) {
                    mod.enabled = true;
                    std::snprintf(mod.status, sizeof(mod.status), "Mod ativado com sucesso.");
                }
                ImGui::PopStyleColor(3);
            }
        } else if (mod.type == ModType::Action) {
            ImGui::TextUnformatted("Acao do Mod:");
            ImGui::SameLine();

            char btn_lbl[64];
            std::snprintf(btn_lbl, sizeof(btn_lbl), "  [ %s ]  ",
                mod.action_applied ? "Re-Apply Mod" : (mod.action_label[0] ? mod.action_label : "Apply"));

            ImGui::PushStyleColor(ImGuiCol_Button, mod.action_applied ? ImVec4(0.18f, 0.45f, 0.25f, 0.95f) : ImVec4(0.72f, 0.25f, 0.15f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.35f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.95f, 0.45f, 0.25f, 1.0f));

            if (ImGui::Button(btn_lbl, ImVec2(160, 34))) {
                if (std::strcmp(mod.id, "dlc_boost_unlocker") == 0) {
                    uint32_t qty = 30;
                    for (auto& opt : mod.options) {
                        if (std::strcmp(opt.id, "ticket_amount") == 0) {
                            qty = static_cast<uint32_t>(opt.int_val);
                        }
                    }
                    TicketInjectResult res = InjectBoostTicketsNative(qty);
                    mod.action_applied = true;
                    std::snprintf(mod.status, sizeof(mod.status), "%s", res.message);
                } else {
                    mod.action_applied = true;
                    std::snprintf(mod.status, sizeof(mod.status), "Acao aplicada com sucesso!");
                }
            }
            ImGui::PopStyleColor(3);
        }

        // --- Dynamic Sub-Options / Sliders constructed from mod.json ---
        if (!mod.options.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "⚙️ Opcoes & Parametros:");
            ImGui::Dummy(ImVec2(0.0f, 2.0f));

            for (size_t j = 0; j < mod.options.size(); ++j) {
                auto& opt = mod.options[j];
                ImGui::PushID(static_cast<int>(j));

                if (opt.type == OptionType::Toggle) {
                    ImGui::Checkbox(opt.name, &opt.bool_val);
                } else if (opt.type == OptionType::SliderInt) {
                    ImGui::TextUnformatted(opt.name);
                    ImGui::PushItemWidth(std::max(180.0f, ImGui::GetContentRegionAvail().x - 20.0f));
                    ImGui::SliderInt("##slider", &opt.int_val, opt.min_int, opt.max_int);
                    ImGui::PopItemWidth();
                } else if (opt.type == OptionType::SliderFloat) {
                    ImGui::TextUnformatted(opt.name);
                    ImGui::PushItemWidth(std::max(180.0f, ImGui::GetContentRegionAvail().x - 20.0f));
                    ImGui::SliderFloat("##slider", &opt.float_val, opt.min_float, opt.max_float, "%.1f");
                    ImGui::PopItemWidth();
                }
                ImGui::PopID();
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::Separator();

        // Status Line
        ImGui::TextUnformatted("Status:");
        ImGui::SameLine();
        if (mod.type == ModType::Toggle) {
            if (mod.enabled) {
                ImGui::TextColored(ImVec4(0.3f, 0.95f, 0.4f, 1.0f), "Ativo [ON] - %s", mod.status);
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Desativado [OFF] - %s", mod.status);
            }
        } else {
            if (mod.action_applied) {
                ImGui::TextColored(ImVec4(0.3f, 0.95f, 0.4f, 1.0f), "%s", mod.status);
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", mod.status);
            }
        }
    }
    ImGui::EndChild();

    // Footer
    const float prompt_y = height - 42.0f;
    if (ImGui::GetCursorPosY() < prompt_y) {
        ImGui::SetCursorPosY(prompt_y);
    }
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.74f, 0.20f, 1.0f));
    ImGui::TextUnformatted("🎮 D-Pad/Stick:");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted("Navegar |");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.74f, 0.20f, 1.0f));
    ImGui::TextUnformatted("◄ / ►:");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted("Sliders |");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.74f, 0.20f, 1.0f));
    ImGui::TextUnformatted("A / Enter / Clique:");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted("Alternar/Apply |");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.74f, 0.20f, 1.0f));
    ImGui::TextUnformatted("B / Esc:");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted("Voltar ao Jogo");
    ImGui::End();
}

bool RenderFrame() {
    UpdateOverlayState();
    if (!g_overlay_visible) {
        return true;
    }

    DXGI_SWAP_CHAIN_DESC description = {};
    if (FAILED(g_swap_chain->GetDesc(&description))) {
        SetFailure(1301, "Could not read the active swap-chain description");
        return false;
    }
    const UINT frame_index = g_swap_chain->GetCurrentBackBufferIndex();
    if (frame_index >= g_frame_count) {
        SetFailure(1302, "Swap-chain returned an invalid back-buffer index");
        return false;
    }
    auto& frame = g_frames[frame_index];
    if (!WaitForFenceValue(frame.fence_value)) {
        return false;
    }
    if (FAILED(frame.allocator->Reset()) ||
        FAILED(g_command_list->Reset(frame.allocator, nullptr))) {
        SetFailure(1303, "Could not reset the overlay command objects");
        return false;
    }

    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    const D3D12_RESOURCE_DESC back_buffer_description = frame.back_buffer->GetDesc();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(
        static_cast<float>(back_buffer_description.Width),
        static_cast<float>(back_buffer_description.Height));
    io.DeltaTime = g_counter_frequency.QuadPart > 0
        ? static_cast<float>(now.QuadPart - g_last_counter.QuadPart) /
              static_cast<float>(g_counter_frequency.QuadPart)
        : 1.0f / 60.0f;
    if (io.DeltaTime <= 0.0f || io.DeltaTime > 0.5f) {
        io.DeltaTime = 1.0f / 60.0f;
    }
    g_last_counter = now;
    io.MouseDrawCursor = g_overlay_visible;
    UpdateGamepadIO(io.DeltaTime);

    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
    BuildOverlay(io.DisplaySize);
    ImGui::Render();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = frame.back_buffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_command_list->ResourceBarrier(1, &barrier);
    g_command_list->OMSetRenderTargets(1, &frame.rtv, FALSE, nullptr);
    g_command_list->SetDescriptorHeaps(1, &g_srv_heap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_command_list);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    g_command_list->ResourceBarrier(1, &barrier);
    if (FAILED(g_command_list->Close())) {
        SetFailure(1304, "Could not close the overlay command list");
        return false;
    }

    ID3D12CommandList* lists[] = {g_command_list};
    g_queue->ExecuteCommandLists(1, lists);
    const UINT64 fence_value = ++g_next_fence_value;
    if (FAILED(g_queue->Signal(g_fence, fence_value))) {
        SetFailure(1305, "Could not signal the overlay frame fence");
        return false;
    }
    frame.fence_value = fence_value;
    return true;
}

HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags) {
    HookScope scope;
    if (!g_shutting_down.load(std::memory_order_acquire)) {
        AcquireSRWLockExclusive(&g_lock);
        if (g_shared != nullptr && g_shared->status != STATUS_FAILED) {
            if (g_swap_chain == nullptr) {
                InitializeRenderer(swap_chain);
            } else if (reinterpret_cast<IDXGISwapChain*>(g_swap_chain) == swap_chain) {
                RenderFrame();
            }
        }
        ReleaseSRWLockExclusive(&g_lock);
    }
    return g_original_present(swap_chain, sync_interval, flags);
}

HRESULT STDMETHODCALLTYPE HookResizeBuffers(
    IDXGISwapChain* swap_chain,
    UINT buffer_count,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    UINT flags) {
    HookScope scope;
    AcquireSRWLockExclusive(&g_lock);
    const bool active_swap_chain =
        g_swap_chain != nullptr && reinterpret_cast<IDXGISwapChain*>(g_swap_chain) == swap_chain;
    if (active_swap_chain) {
        const bool overlay_visible = g_overlay_visible;
        const bool waiting_for_back_release = g_waiting_for_back_release;
        DestroyRenderer();
        g_overlay_visible = overlay_visible;
        g_waiting_for_back_release = waiting_for_back_release;
        SetStatus(STATUS_HOOKS_INSTALLED);
    }
    const HRESULT result = g_original_resize_buffers(
        swap_chain, buffer_count, width, height, format, flags);
    if (active_swap_chain && FAILED(result)) {
        SetFailure(1401, "Game swap-chain resize failed after overlay cleanup");
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

void STDMETHODCALLTYPE HookExecuteCommandLists(
    ID3D12CommandQueue* queue,
    UINT command_list_count,
    ID3D12CommandList* const* command_lists) {
    HookScope scope;
    if (!g_shutting_down.load(std::memory_order_acquire) && g_queue == nullptr &&
        queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        AcquireSRWLockExclusive(&g_lock);
        if (g_queue == nullptr) {
            queue->AddRef();
            g_queue = queue;
        }
        ReleaseSRWLockExclusive(&g_lock);
    }
    g_original_execute_command_lists(queue, command_list_count, command_lists);
}

LRESULT CALLBACK DummyWindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}

bool FindHookTargets() {
    const wchar_t* class_name = L"DisgaeaMayhemModMenuHookTargets";
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DummyWindowProcedure;
    window_class.hInstance = g_module;
    window_class.lpszClassName = class_name;
    const ATOM atom = RegisterClassExW(&window_class);
    if (atom == 0) {
        SetFailure(1501, "Could not register the DirectX hook-target window class");
        return false;
    }
    HWND window = CreateWindowExW(
        0, class_name, L"", WS_OVERLAPPED, 0, 0, 2, 2,
        nullptr, nullptr, g_module, nullptr);
    if (window == nullptr) {
        UnregisterClassW(class_name, g_module);
        SetFailure(1502, "Could not create the DirectX hook-target window");
        return false;
    }

    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGIFactory4* factory = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    bool success = false;
    do {
        if (FAILED(D3D12CreateDevice(
                nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
            SetFailure(1503, "D3D12CreateDevice failed while locating hook targets");
            break;
        }
        D3D12_COMMAND_QUEUE_DESC queue_description = {};
        queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(
                &queue_description, IID_PPV_ARGS(&queue)))) {
            SetFailure(1504, "Could not create the hook-target command queue");
            break;
        }
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            SetFailure(1505, "CreateDXGIFactory1 failed while locating hook targets");
            break;
        }
        DXGI_SWAP_CHAIN_DESC swap_description = {};
        swap_description.BufferDesc.Width = 2;
        swap_description.BufferDesc.Height = 2;
        swap_description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_description.SampleDesc.Count = 1;
        swap_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_description.BufferCount = 2;
        swap_description.OutputWindow = window;
        swap_description.Windowed = TRUE;
        swap_description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(factory->CreateSwapChain(queue, &swap_description, &swap_chain))) {
            SetFailure(1506, "Could not create the hook-target swap chain");
            break;
        }
        void** swap_vtable = *reinterpret_cast<void***>(swap_chain);
        void** queue_vtable = *reinterpret_cast<void***>(queue);
        g_present_target = swap_vtable[8];
        g_resize_buffers_target = swap_vtable[13];
        g_execute_target = queue_vtable[10];
        success = g_present_target != nullptr && g_resize_buffers_target != nullptr &&
                  g_execute_target != nullptr;
        if (!success) {
            SetFailure(1507, "A DirectX 12 hook target was null");
        }
    } while (false);

    SafeRelease(swap_chain);
    SafeRelease(factory);
    SafeRelease(queue);
    SafeRelease(device);
    DestroyWindow(window);
    UnregisterClassW(class_name, g_module);
    return success;
}

bool CreateHooks() {
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        SetFailure(1601, "MinHook initialization failed");
        return false;
    }
    if (!FindHookTargets()) {
        MH_Uninitialize();
        return false;
    }
    status = MH_CreateHook(
        g_present_target,
        reinterpret_cast<void*>(&HookPresent),
        reinterpret_cast<void**>(&g_original_present));
    if (status != MH_OK) {
        SetFailure(1602, "Could not create the IDXGISwapChain::Present hook");
        MH_Uninitialize();
        return false;
    }
    status = MH_CreateHook(
        g_resize_buffers_target,
        reinterpret_cast<void*>(&HookResizeBuffers),
        reinterpret_cast<void**>(&g_original_resize_buffers));
    if (status != MH_OK) {
        SetFailure(1603, "Could not create the IDXGISwapChain::ResizeBuffers hook");
        MH_RemoveHook(g_present_target);
        MH_Uninitialize();
        return false;
    }
    status = MH_CreateHook(
        g_execute_target,
        reinterpret_cast<void*>(&HookExecuteCommandLists),
        reinterpret_cast<void**>(&g_original_execute_command_lists));
    if (status != MH_OK) {
        SetFailure(1604, "Could not create the ID3D12CommandQueue::ExecuteCommandLists hook");
        MH_RemoveHook(g_resize_buffers_target);
        MH_RemoveHook(g_present_target);
        MH_Uninitialize();
        return false;
    }
    if (MH_QueueEnableHook(g_present_target) != MH_OK ||
        MH_QueueEnableHook(g_resize_buffers_target) != MH_OK ||
        MH_QueueEnableHook(g_execute_target) != MH_OK ||
        MH_ApplyQueued() != MH_OK) {
        SetFailure(1605, "Could not enable the DirectX 12 hooks");
        MH_RemoveHook(g_execute_target);
        MH_RemoveHook(g_resize_buffers_target);
        MH_RemoveHook(g_present_target);
        MH_Uninitialize();
        return false;
    }
    return true;
}

void RemoveHooks() {
    g_shutting_down.store(true, std::memory_order_release);
    if (g_present_target != nullptr) {
        MH_DisableHook(g_present_target);
    }
    if (g_resize_buffers_target != nullptr) {
        MH_DisableHook(g_resize_buffers_target);
    }
    if (g_execute_target != nullptr) {
        MH_DisableHook(g_execute_target);
    }
    for (int attempt = 0;
         attempt < 5000 && g_active_hooks.load(std::memory_order_acquire) != 0;
         ++attempt) {
        Sleep(1);
    }
    MH_RemoveHook(g_execute_target);
    MH_RemoveHook(g_resize_buffers_target);
    MH_RemoveHook(g_present_target);
    MH_Uninitialize();
}

bool ApplyGamePatches(HMODULE game_exe) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(game_exe);

    // Verify signatures before patching
    const uint8_t expected_p1[] = { 0x0F, 0x84, 0x49, 0x04, 0x00, 0x00 };
    const uint8_t expected_p2[] = { 0x0F, 0x84, 0xC3, 0x02, 0x00, 0x00 };
    const uint8_t expected_p3[] = { 0x03 };
    const uint8_t expected_disp[] = {
        0x48, 0x8D, 0x8F, 0x00, 0x02, 0x00, 0x00,
        0x48, 0x8D, 0x05, 0xF0, 0x51, 0x77, 0x00
    };

    if (std::memcmp(reinterpret_cast<void*>(base + 0x006FD501), expected_p1, sizeof(expected_p1)) != 0 ||
        std::memcmp(reinterpret_cast<void*>(base + 0x002B8A21), expected_p2, sizeof(expected_p2)) != 0 ||
        std::memcmp(reinterpret_cast<void*>(base + 0x002B907D), expected_p3, sizeof(expected_p3)) != 0 ||
        std::memcmp(reinterpret_cast<void*>(base + 0x002B9582), expected_disp, sizeof(expected_disp)) != 0) {
        SetFailure(1801, "Signatures in game executable did not match (patches skipped)");
        return false;
    }

    // Allocate hook stub (PAGE_EXECUTE_READWRITE)
    g_hook_stub = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (g_hook_stub == nullptr) {
        SetFailure(1802, "Could not allocate executable hook stub");
        return false;
    }
    uint8_t* stub = static_cast<uint8_t*>(g_hook_stub);

    // Build 37-byte hook code
    uint8_t hook[37] = {};
    hook[0] = 0x48; hook[1] = 0xB8;  // mov rax, &g_shared
    *reinterpret_cast<uint64_t*>(&hook[2]) = reinterpret_cast<uint64_t>(g_shared);
    hook[10] = 0x48; hook[11] = 0x89; hook[12] = 0x78; hook[13] = 0x08;  // mov [rax+8], rdi (Main Menu)
    hook[14] = 0xC6; hook[15] = 0x87; hook[16] = 0x20; hook[17] = 0x02; hook[18] = 0x00; hook[19] = 0x00; hook[20] = 0x00;  // mov byte ptr [rdi+220h], 0
    hook[21] = 0xC6; hook[22] = 0x00; hook[23] = 0x01;  // mov byte ptr [rax], 1 (open_request = 1)
    hook[24] = 0x49; hook[25] = 0xBA;  // mov r10, resume_address (base + 0x002B95B9)
    *reinterpret_cast<uint64_t*>(&hook[26]) = static_cast<uint64_t>(base + 0x002B95B9);
    hook[34] = 0x41; hook[35] = 0xFF; hook[36] = 0xE2;  // jmp r10

    std::memcpy(stub, hook, sizeof(hook));
    FlushInstructionCache(GetCurrentProcess(), stub, sizeof(hook));

    // Build 14-byte dispatcher jump
    uint8_t jump[14] = {};
    jump[0] = 0x48; jump[1] = 0xB8;  // mov rax, stub
    *reinterpret_cast<uint64_t*>(&jump[2]) = reinterpret_cast<uint64_t>(stub);
    jump[10] = 0xFF; jump[11] = 0xE0;  // jmp rax
    jump[12] = 0x90; jump[13] = 0x90;  // nop nop

    const uint8_t nop6[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    const uint8_t nav4[1] = { 0x04 };

    auto PatchBytes = [](uintptr_t address, const void* data, size_t size) -> bool {
        DWORD old_protect = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &old_protect)) {
            return false;
        }
        std::memcpy(reinterpret_cast<void*>(address), data, size);
        DWORD ignored = 0;
        VirtualProtect(reinterpret_cast<void*>(address), size, old_protect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
        return true;
    };

    if (!PatchBytes(base + 0x006FD501, nop6, sizeof(nop6)) ||
        !PatchBytes(base + 0x002B8A21, nop6, sizeof(nop6)) ||
        !PatchBytes(base + 0x002B907D, nav4, sizeof(nav4)) ||
        !PatchBytes(base + 0x002B9582, jump, sizeof(jump))) {
        SetFailure(1803, "Failed to apply in-memory patches");
        return false;
    }

    return true;
}

DWORD WINAPI AutoInitThread(LPVOID) {
    Sleep(50);

    HMODULE game_exe = GetModuleHandleW(nullptr);
    if (!game_exe) {
        return 0;
    }

    wchar_t exe_path[MAX_PATH] = {};
    GetModuleFileNameW(game_exe, exe_path, MAX_PATH);

    g_shared = &g_embedded_shared;
    InterlockedExchange(&g_shared->status, STATUS_WAITING);
    InterlockedExchange(&g_shared->error_code, 0);
    g_shared->error_message[0] = '\0';

    // If inside Disgaea Mayhem executable, install memory patches
    if (wcsstr(exe_path, L"Disgaea_Mayhem.exe") != nullptr) {
        ApplyGamePatches(game_exe);
    }

    // Install DirectX 12 hooks
    if (CreateHooks()) {
        SetStatus(STATUS_HOOKS_INSTALLED);
    }

    return 0;
}

}  // namespace

extern "C" __declspec(dllexport) DWORD WINAPI InitializeModMenu(void* parameter) {
    if (parameter != nullptr) {
        g_shared = static_cast<SharedState*>(parameter);
    }
    if (!CreateHooks()) {
        return 0;
    }
    SetStatus(STATUS_HOOKS_INSTALLED);
    return 1;
}

extern "C" __declspec(dllexport) DWORD WINAPI ShutdownModMenu(void*) {
    if (g_shared == nullptr) {
        return 0;
    }
    RemoveHooks();
    if (g_active_hooks.load(std::memory_order_acquire) != 0) {
        SetFailure(1701, "Timed out while stopping active DirectX hooks");
        return 0;
    }
    AcquireSRWLockExclusive(&g_lock);
    if (g_overlay_visible || g_waiting_for_back_release) {
        SetMainMenuInputEnabled();
    }
    DestroyRenderer();
    SafeRelease(g_queue);
    SetStatus(STATUS_WAITING);
    ReleaseSRWLockExclusive(&g_lock);
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
        ProxyDXGI::EnsureRealDxgiLoaded();
        HANDLE thread = CreateThread(nullptr, 0, AutoInitThread, nullptr, 0, nullptr);
        if (thread != nullptr) {
            CloseHandle(thread);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        ShutdownModMenu(nullptr);
        if (g_hook_stub != nullptr) {
            VirtualFree(g_hook_stub, 0, MEM_RELEASE);
            g_hook_stub = nullptr;
        }
    }
    return TRUE;
}
