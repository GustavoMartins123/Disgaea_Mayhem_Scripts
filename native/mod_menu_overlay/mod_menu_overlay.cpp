#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <xinput.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "dm_mod_common.h"

// -----------------------------------------------------------------------------
// Mod Menu Engine & Overlay Implementation
// -----------------------------------------------------------------------------
namespace {

constexpr LONG STATUS_WAITING = 0;
constexpr LONG STATUS_HOOKS_INSTALLED = 1;
constexpr LONG STATUS_RENDER_READY = 2;
constexpr LONG STATUS_FAILED = -1;
constexpr UINT SRV_DESCRIPTOR_COUNT = 64;

struct OverlayStatus {
    volatile LONG status;
    volatile LONG error_code;
    char error_message[256];
};

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
OverlayStatus g_shared = {};

PresentFn g_original_present = nullptr;
ResizeBuffersFn g_original_resize_buffers = nullptr;
ExecuteCommandListsFn g_original_execute_command_lists = nullptr;
void* g_present_target = nullptr;
void* g_resize_buffers_target = nullptr;
void* g_execute_target = nullptr;
std::atomic<LONG> g_active_hooks{0};
std::atomic<bool> g_shutting_down{false};
std::atomic<bool> g_hooks_installed{false};
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
std::atomic<bool> g_overlay_visible{false};
std::atomic<bool> g_waiting_for_back_release{false};
bool g_last_b_down = false;
bool g_last_escape_down = false;
LARGE_INTEGER g_last_counter = {};
LARGE_INTEGER g_counter_frequency = {};

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
    InterlockedExchange(&g_shared.status, status);
}

void SetFailure(LONG code, const char* message) {
    InterlockedExchange(&g_shared.error_code, code);
    std::snprintf(g_shared.error_message, sizeof(g_shared.error_message), "%s", message);
    MemoryBarrier();
    InterlockedExchange(&g_shared.status, STATUS_FAILED);
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

typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD dwUserIndex, XINPUT_STATE* pState);
static PFN_XInputGetState g_pfnXInputGetState = nullptr;
static bool g_xinput_loaded = false;

inline void EnsureXInputLoaded() {
    if (g_xinput_loaded) return;
    HMODULE h = LoadLibraryW(L"xinput9_1_0.dll");
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

constexpr ULONGLONG PAD_RESCAN_INTERVAL_MS = 1000;

struct InputSnapshot {
    WORD pad_buttons = 0;
    SHORT lx = 0;
    SHORT ly = 0;
    SHORT rx = 0;
    SHORT ry = 0;
    bool pad_menu_hotkey = false;
    bool pad_b = false;
    bool pad_max = false;
    bool pad_min = false;
    bool key_menu_hotkey = false;
    bool key_escape = false;
    bool key_max = false;
    bool key_min = false;
};

InputSnapshot g_input = {};
DWORD g_connected_pad_slots = 0;
ULONGLONG g_next_pad_rescan = 0;

void PollInput() {
    InputSnapshot snapshot = {};
    snapshot.key_escape = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    snapshot.key_max = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
    snapshot.key_min = (GetAsyncKeyState(VK_END) & 0x8000) != 0;
    snapshot.key_menu_hotkey = (GetAsyncKeyState(VK_F1) & 0x8000) != 0 ||
                               (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0 ||
                               (snapshot.key_max &&
                                !g_overlay_visible.load(std::memory_order_acquire));

    const ULONGLONG now = GetTickCount64();
    const bool rescan = now >= g_next_pad_rescan;
    if (rescan) {
        g_next_pad_rescan = now + PAD_RESCAN_INTERVAL_MS;
    }

    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
        const DWORD slot = 1u << index;
        if ((g_connected_pad_slots & slot) == 0 && !rescan) {
            continue;
        }
        XINPUT_STATE state = {};
        if (SafeXInputGetState(index, &state) != ERROR_SUCCESS) {
            g_connected_pad_slots &= ~slot;
            continue;
        }
        g_connected_pad_slots |= slot;

        const WORD buttons = state.Gamepad.wButtons;
        snapshot.pad_buttons |= buttons;
        snapshot.pad_b = snapshot.pad_b || (buttons & XINPUT_GAMEPAD_B) != 0;
        // L3 + R3 (both thumbsticks clicked) OR Select / Back button
        const bool thumbs = (buttons & XINPUT_GAMEPAD_LEFT_THUMB) != 0 &&
                            (buttons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
        snapshot.pad_menu_hotkey = snapshot.pad_menu_hotkey || thumbs ||
                                   (buttons & XINPUT_GAMEPAD_BACK) != 0;
        snapshot.pad_max = snapshot.pad_max ||
                           state.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
        snapshot.pad_min = snapshot.pad_min ||
                           state.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;

        if (std::abs(state.Gamepad.sThumbLX) > std::abs(snapshot.lx)) snapshot.lx = state.Gamepad.sThumbLX;
        if (std::abs(state.Gamepad.sThumbLY) > std::abs(snapshot.ly)) snapshot.ly = state.Gamepad.sThumbLY;
        if (std::abs(state.Gamepad.sThumbRX) > std::abs(snapshot.rx)) snapshot.rx = state.Gamepad.sThumbRX;
        if (std::abs(state.Gamepad.sThumbRY) > std::abs(snapshot.ry)) snapshot.ry = state.Gamepad.sThumbRY;
    }
    g_input = snapshot;
}

bool ControllerBDown() { return g_input.pad_b; }

bool EscapeDown() { return g_input.key_escape; }

bool ModMenuHotkeyKeyboardDown() { return g_input.key_menu_hotkey; }

bool ModMenuHotkeyKeyboardPressed() {
    static bool was_pressed = false;
    const bool is_pressed = ModMenuHotkeyKeyboardDown();
    bool triggered = is_pressed && !was_pressed;
    was_pressed = is_pressed;
    return triggered;
}

bool ModMenuHotkeyGamepadDown() { return g_input.pad_menu_hotkey; }

bool ModMenuHotkeyGamepadPressed() {
    static bool was_pressed = false;
    const bool is_pressed = ModMenuHotkeyGamepadDown();
    bool triggered = is_pressed && !was_pressed;
    was_pressed = is_pressed;
    return triggered;
}

bool InputCaptureActive() {
    return g_overlay_visible.load(std::memory_order_acquire) ||
           g_waiting_for_back_release.load(std::memory_order_acquire);
}

void OpenOverlay() {
    g_waiting_for_back_release.store(false, std::memory_order_release);
    g_overlay_visible.store(true, std::memory_order_release);
}

void CloseOverlay() {
    g_waiting_for_back_release.store(true, std::memory_order_release);
    g_overlay_visible.store(false, std::memory_order_release);
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

ImGuiKey VirtualKeyToImGuiKey(WPARAM virtual_key, LPARAM lparam) {
    if (virtual_key >= '0' && virtual_key <= '9') {
        return static_cast<ImGuiKey>(ImGuiKey_0 + (virtual_key - '0'));
    }
    if (virtual_key >= 'A' && virtual_key <= 'Z') {
        return static_cast<ImGuiKey>(ImGuiKey_A + (virtual_key - 'A'));
    }
    if (virtual_key >= VK_F1 && virtual_key <= VK_F12) {
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (virtual_key - VK_F1));
    }

    switch (virtual_key) {
        case VK_TAB: return ImGuiKey_Tab;
        case VK_LEFT: return ImGuiKey_LeftArrow;
        case VK_RIGHT: return ImGuiKey_RightArrow;
        case VK_UP: return ImGuiKey_UpArrow;
        case VK_DOWN: return ImGuiKey_DownArrow;
        case VK_PRIOR: return ImGuiKey_PageUp;
        case VK_NEXT: return ImGuiKey_PageDown;
        case VK_HOME: return ImGuiKey_Home;
        case VK_END: return ImGuiKey_End;
        case VK_INSERT: return ImGuiKey_Insert;
        case VK_DELETE: return ImGuiKey_Delete;
        case VK_BACK: return ImGuiKey_Backspace;
        case VK_SPACE: return ImGuiKey_Space;
        case VK_RETURN: return ImGuiKey_Enter;
        case VK_ESCAPE: return ImGuiKey_Escape;
        case VK_LCONTROL: return ImGuiKey_LeftCtrl;
        case VK_RCONTROL: return ImGuiKey_RightCtrl;
        case VK_LSHIFT: return ImGuiKey_LeftShift;
        case VK_RSHIFT: return ImGuiKey_RightShift;
        case VK_LMENU: return ImGuiKey_LeftAlt;
        case VK_RMENU: return ImGuiKey_RightAlt;
        case VK_LWIN: return ImGuiKey_LeftSuper;
        case VK_RWIN: return ImGuiKey_RightSuper;
        case VK_CONTROL:
            return (lparam & 0x01000000) != 0 ? ImGuiKey_RightCtrl : ImGuiKey_LeftCtrl;
        case VK_MENU:
            return (lparam & 0x01000000) != 0 ? ImGuiKey_RightAlt : ImGuiKey_LeftAlt;
        case VK_SHIFT: {
            const UINT scan_code = static_cast<UINT>((lparam >> 16) & 0xFF);
            return MapVirtualKeyW(scan_code, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT
                ? ImGuiKey_RightShift
                : ImGuiKey_LeftShift;
        }
        default: return ImGuiKey_None;
    }
}

bool IsMouseInputMessage(UINT message) {
    return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
           message == WM_MOUSELEAVE || message == WM_MOUSEHOVER;
}

bool IsKeyboardInputMessage(UINT message) {
    return (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
           message == WM_SYSKEYDOWN || message == WM_SYSKEYUP ||
           message == WM_UNICHAR;
}

LRESULT CALLBACK ModMenuWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (g_imgui_context_created && InputCaptureActive()) {
        ImGuiIO& io = ImGui::GetIO();
        const bool visible = g_overlay_visible.load(std::memory_order_acquire);

        if (visible) {
            switch (msg) {
                case WM_LBUTTONDOWN:
                case WM_LBUTTONDBLCLK: io.AddMouseButtonEvent(0, true); break;
                case WM_LBUTTONUP: io.AddMouseButtonEvent(0, false); break;
                case WM_RBUTTONDOWN:
                case WM_RBUTTONDBLCLK: io.AddMouseButtonEvent(1, true); break;
                case WM_RBUTTONUP: io.AddMouseButtonEvent(1, false); break;
                case WM_MBUTTONDOWN:
                case WM_MBUTTONDBLCLK: io.AddMouseButtonEvent(2, true); break;
                case WM_MBUTTONUP: io.AddMouseButtonEvent(2, false); break;
                case WM_XBUTTONDOWN:
                case WM_XBUTTONDBLCLK:
                    io.AddMouseButtonEvent(GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? 3 : 4, true);
                    break;
                case WM_XBUTTONUP:
                    io.AddMouseButtonEvent(GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? 3 : 4, false);
                    break;
                case WM_MOUSEMOVE:
                    io.AddMousePosEvent(
                        static_cast<float>(static_cast<short>(LOWORD(lparam))),
                        static_cast<float>(static_cast<short>(HIWORD(lparam))));
                    break;
                case WM_MOUSELEAVE: io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); break;
                case WM_MOUSEWHEEL:
                    io.AddMouseWheelEvent(
                        0.0f,
                        static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                            static_cast<float>(WHEEL_DELTA));
                    break;
                case WM_MOUSEHWHEEL:
                    io.AddMouseWheelEvent(
                        static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                            static_cast<float>(WHEEL_DELTA),
                        0.0f);
                    break;
                case WM_KEYDOWN:
                case WM_SYSKEYDOWN:
                case WM_KEYUP:
                case WM_SYSKEYUP: {
                    const ImGuiKey key = VirtualKeyToImGuiKey(wparam, lparam);
                    if (key != ImGuiKey_None) {
                        io.AddKeyEvent(key, msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
                    }
                    break;
                }
                case WM_CHAR:
                    if (wparam > 0 && wparam < 0x10000) {
                        io.AddInputCharacter(static_cast<unsigned int>(wparam));
                    }
                    break;
                case WM_UNICHAR:
                    if (wparam == UNICODE_NOCHAR) return TRUE;
                    io.AddInputCharacter(static_cast<unsigned int>(wparam));
                    break;
                case WM_SETFOCUS: io.AddFocusEvent(true); break;
                case WM_KILLFOCUS: io.AddFocusEvent(false); break;
                case WM_SETCURSOR:
                    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
                    return TRUE;
            }
        }

        if (msg == WM_INPUT) {
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        if (IsMouseInputMessage(msg) || IsKeyboardInputMessage(msg) ||
            msg == WM_INPUT_DEVICE_CHANGE || msg == WM_APPCOMMAND) {
            return msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP || msg == WM_XBUTTONDBLCLK
                ? TRUE
                : 0;
        }
    }
    return CallWindowProcW(g_original_wndproc, hwnd, msg, wparam, lparam);
}

using KeyboardQueryFn = bool (*)(void*, int);
using MouseButtonQueryFn = bool (*)(void*, int, int);
using MouseAxisQueryFn = LONG (*)(void*, int);
using PadRawDataFn = void* (*)(void*);
using NmplInputUpdateFn = void (*)(float);
using PadManagerGetInstanceFn = void* (*)();
using PadManagerGetPadFn = void* (*)(void*, int);
using PadClearButtonsFn = void (*)(void*, std::uint64_t);
using PadStopRepeatFn = void (*)(void*);

KeyboardQueryFn g_original_keyboard_press = nullptr;
KeyboardQueryFn g_original_keyboard_trigger = nullptr;
KeyboardQueryFn g_original_keyboard_repeat = nullptr;
KeyboardQueryFn g_original_keyboard_release = nullptr;
MouseButtonQueryFn g_original_mouse_press = nullptr;
MouseButtonQueryFn g_original_mouse_trigger = nullptr;
MouseButtonQueryFn g_original_mouse_repeat = nullptr;
MouseButtonQueryFn g_original_mouse_release = nullptr;
MouseAxisQueryFn g_original_mouse_axis_x = nullptr;
MouseAxisQueryFn g_original_mouse_axis_y = nullptr;
PadRawDataFn g_original_pad_raw_data = nullptr;
NmplInputUpdateFn g_original_nmpl_input_update = nullptr;
PadManagerGetInstanceFn g_pad_manager_get_instance = nullptr;
PadManagerGetPadFn g_pad_manager_get_pad = nullptr;
PadClearButtonsFn g_pad_clear_buttons = nullptr;
PadStopRepeatFn g_pad_stop_repeat = nullptr;
alignas(16) std::array<std::uint8_t, 0x138> g_neutral_pad_data = {};

bool HookKeyboardPress(void* keyboard, int key) {
    HookScope scope;
    return InputCaptureActive() ? false : g_original_keyboard_press(keyboard, key);
}

bool HookKeyboardTrigger(void* keyboard, int key) {
    HookScope scope;
    return InputCaptureActive() ? false : g_original_keyboard_trigger(keyboard, key);
}

bool HookKeyboardRepeat(void* keyboard, int key) {
    HookScope scope;
    return InputCaptureActive() ? false : g_original_keyboard_repeat(keyboard, key);
}

bool HookKeyboardRelease(void* keyboard, int key) {
    HookScope scope;
    return InputCaptureActive() ? false : g_original_keyboard_release(keyboard, key);
}

bool HookMousePress(void* mouse, int button, int port) {
    HookScope scope;
    return InputCaptureActive() ? false : g_original_mouse_press(mouse, button, port);
}

bool HookMouseTrigger(void* mouse, int button, int port) {
    HookScope scope;
    return InputCaptureActive() ? false : g_original_mouse_trigger(mouse, button, port);
}

bool HookMouseRepeat(void* mouse, int button, int port) {
    HookScope scope;
    return InputCaptureActive() ? false : g_original_mouse_repeat(mouse, button, port);
}

bool HookMouseRelease(void* mouse, int button, int port) {
    HookScope scope;
    return InputCaptureActive() ? false : g_original_mouse_release(mouse, button, port);
}

LONG HookMouseAxisX(void* mouse, int port) {
    HookScope scope;
    return InputCaptureActive() ? 0 : g_original_mouse_axis_x(mouse, port);
}

LONG HookMouseAxisY(void* mouse, int port) {
    HookScope scope;
    return InputCaptureActive() ? 0 : g_original_mouse_axis_y(mouse, port);
}

void* HookPadRawData(void* pad) {
    HookScope scope;
    return InputCaptureActive()
        ? static_cast<void*>(g_neutral_pad_data.data())
        : g_original_pad_raw_data(pad);
}

void NeutralizeNmplPads() {
    void* manager = g_pad_manager_get_instance();
    if (manager == nullptr) return;

    for (int index = 0; index < 4; ++index) {
        void* pad = g_pad_manager_get_pad(manager, index);
        if (!IsWritableAddress(pad, 0x26C)) continue;

        g_pad_clear_buttons(pad, UINT64_MAX);
        g_pad_stop_repeat(pad);
        auto* bytes = reinterpret_cast<std::uint8_t*>(pad);
        std::memset(bytes + 0x40, 0, 8);
        std::memset(bytes + 0x258, 0, 0x14);
    }
}

void HookNmplInputUpdate(float delta_time) {
    HookScope scope;
    g_original_nmpl_input_update(delta_time);
    if (InputCaptureActive()) NeutralizeNmplPads();
}

struct NmplInputHookSpec {
    const char* export_name;
    void* detour;
    void** original;
    void* target = nullptr;
};

NmplInputHookSpec g_nmpl_input_hooks[] = {
    {"?press@CKeyboard@Input@Nmpl@@QEAA_NH@Z", reinterpret_cast<void*>(&HookKeyboardPress),
     reinterpret_cast<void**>(&g_original_keyboard_press)},
    {"?trigger@CKeyboard@Input@Nmpl@@QEAA_NH@Z", reinterpret_cast<void*>(&HookKeyboardTrigger),
     reinterpret_cast<void**>(&g_original_keyboard_trigger)},
    {"?repeat@CKeyboard@Input@Nmpl@@QEAA_NH@Z", reinterpret_cast<void*>(&HookKeyboardRepeat),
     reinterpret_cast<void**>(&g_original_keyboard_repeat)},
    {"?release@CKeyboard@Input@Nmpl@@QEAA_NH@Z", reinterpret_cast<void*>(&HookKeyboardRelease),
     reinterpret_cast<void**>(&g_original_keyboard_release)},
    {"?isPress@CMouseWin@Input@Nmpl@@UEBA_NW4EMouseButton@CMouseBase@23@H@Z",
     reinterpret_cast<void*>(&HookMousePress), reinterpret_cast<void**>(&g_original_mouse_press)},
    {"?isTrigger@CMouseWin@Input@Nmpl@@UEBA_NW4EMouseButton@CMouseBase@23@H@Z",
     reinterpret_cast<void*>(&HookMouseTrigger), reinterpret_cast<void**>(&g_original_mouse_trigger)},
    {"?isRepeat@CMouseWin@Input@Nmpl@@UEBA_NW4EMouseButton@CMouseBase@23@H@Z",
     reinterpret_cast<void*>(&HookMouseRepeat), reinterpret_cast<void**>(&g_original_mouse_repeat)},
    {"?isRelease@CMouseWin@Input@Nmpl@@UEBA_NW4EMouseButton@CMouseBase@23@H@Z",
     reinterpret_cast<void*>(&HookMouseRelease), reinterpret_cast<void**>(&g_original_mouse_release)},
    {"?axisX@CMouseWin@Input@Nmpl@@UEBAJH@Z", reinterpret_cast<void*>(&HookMouseAxisX),
     reinterpret_cast<void**>(&g_original_mouse_axis_x)},
    {"?axisY@CMouseWin@Input@Nmpl@@UEBAJH@Z", reinterpret_cast<void*>(&HookMouseAxisY),
     reinterpret_cast<void**>(&g_original_mouse_axis_y)},
    {"?rawData@CPad@Input@Nmpl@@QEAAPEAXXZ", reinterpret_cast<void*>(&HookPadRawData),
     reinterpret_cast<void**>(&g_original_pad_raw_data)},
    {"?update@CNmplInput@Input@Nmpl@@SAXM@Z", reinterpret_cast<void*>(&HookNmplInputUpdate),
     reinterpret_cast<void**>(&g_original_nmpl_input_update)},
};

void RemoveNmplInputHooks() {
    for (auto& hook : g_nmpl_input_hooks) {
        if (hook.target != nullptr) {
            dm::RemoveHook(hook.target);
            hook.target = nullptr;
        }
    }
}

bool CreateNmplInputHooks() {
    HMODULE nmpl = GetModuleHandleW(L"NmplDLL.dll");
    if (nmpl == nullptr) {
        SetFailure(1606, "NmplDLL.dll is not loaded; exclusive input capture is unavailable");
        return false;
    }

    g_pad_manager_get_instance = reinterpret_cast<PadManagerGetInstanceFn>(
        reinterpret_cast<void*>(
            GetProcAddress(nmpl, "?getInstance@CPadMgr@Input@Nmpl@@SAAEAV123@XZ")));
    g_pad_manager_get_pad = reinterpret_cast<PadManagerGetPadFn>(
        reinterpret_cast<void*>(
            GetProcAddress(nmpl, "?pad@CPadMgr@Input@Nmpl@@QEAAAEAVCPad@23@H@Z")));
    g_pad_clear_buttons = reinterpret_cast<PadClearButtonsFn>(
        reinterpret_cast<void*>(
            GetProcAddress(nmpl, "?clearBtn@CPad@Input@Nmpl@@QEAAX_K@Z")));
    g_pad_stop_repeat = reinterpret_cast<PadStopRepeatFn>(
        reinterpret_cast<void*>(
            GetProcAddress(nmpl, "?reptStop@CPad@Input@Nmpl@@QEAAXXZ")));
    if (g_pad_manager_get_instance == nullptr || g_pad_manager_get_pad == nullptr ||
        g_pad_clear_buttons == nullptr || g_pad_stop_repeat == nullptr) {
        SetFailure(1607, "A required Nmpl pad-control export was not found");
        return false;
    }

    for (auto& hook : g_nmpl_input_hooks) {
        hook.target = reinterpret_cast<void*>(GetProcAddress(nmpl, hook.export_name));
        if (hook.target == nullptr) {
            SetFailure(1607, "A required Nmpl input export was not found");
            RemoveNmplInputHooks();
            return false;
        }
    }

    for (auto& hook : g_nmpl_input_hooks) {
        if (!dm::CreateHook(hook.target, hook.detour, hook.original)) {
            SetFailure(1608, "Could not create an Nmpl exclusive-input hook");
            RemoveNmplInputHooks();
            return false;
        }
    }
    return true;
}

bool QueueEnableNmplInputHooks() {
    for (const auto& hook : g_nmpl_input_hooks) {
        if (hook.target == nullptr || !dm::QueueHook(hook.target, true)) return false;
    }
    return true;
}

void DisableNmplInputHooks() {
    for (const auto& hook : g_nmpl_input_hooks) {
        if (hook.target != nullptr) dm::QueueHook(hook.target, false);
    }
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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableSetMousePos;
    io.ConfigNavCaptureKeyboard = true;

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

void ScanAndDiscoverMods();
void FlushAllModConfigs();

void UpdateOverlayState() {
    PollInput();
    const bool was_visible = g_overlay_visible.load(std::memory_order_acquire);
    if (ModMenuHotkeyKeyboardPressed() || ModMenuHotkeyGamepadPressed()) {
        if (g_overlay_visible.load(std::memory_order_acquire)) {
            CloseOverlay();
        } else {
            OpenOverlay();
        }
    }

    const bool b_down = ControllerBDown();
    const bool escape_down = EscapeDown();
    if (g_overlay_visible.load(std::memory_order_acquire) &&
        ((b_down && !g_last_b_down) || (escape_down && !g_last_escape_down))) {
        CloseOverlay();
    }
    g_last_b_down = b_down;
    g_last_escape_down = escape_down;

    if (!g_overlay_visible.load(std::memory_order_acquire) &&
        g_waiting_for_back_release.load(std::memory_order_acquire) &&
        !b_down && !escape_down && !ModMenuHotkeyKeyboardDown() &&
        !ModMenuHotkeyGamepadDown()) {
        g_waiting_for_back_release.store(false, std::memory_order_release);
    }

    const bool visible = g_overlay_visible.load(std::memory_order_acquire);
    if (!was_visible && visible) {
        ScanAndDiscoverMods();
    }
    if (was_visible && !visible) {
        FlushAllModConfigs();
    }
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
    char id[64] = {};
    char name[128] = {};
    char category[64] = {};
    char version[32] = {};
    char author[64] = {};
    char description[512] = {};
    char action_label[32] = {};
    ModType type = ModType::Toggle;
    bool enabled = false;
    char status[192] = {};
    bool action_applied = false;
    std::vector<ModOption> options;
};

static std::vector<ModItem> g_discovered_mods;
static int g_selected_mod = 0;
static bool g_mods_scanned = false;

static const DmModLoaderApi* g_loader_api = nullptr;

bool CopyLoaderView(const DmModView& view, ModItem& mod) {
    if (view.struct_size != sizeof(DmModView) || view.option_count > DM_MAX_MOD_OPTIONS) {
        return false;
    }

    ModItem parsed = {};
    std::snprintf(parsed.dir_name, sizeof(parsed.dir_name), "%s", view.directory);
    std::snprintf(parsed.id, sizeof(parsed.id), "%s", view.id);
    std::snprintf(parsed.name, sizeof(parsed.name), "%s", view.name);
    std::snprintf(parsed.category, sizeof(parsed.category), "%s", view.category);
    std::snprintf(parsed.version, sizeof(parsed.version), "%s", view.version);
    std::snprintf(parsed.author, sizeof(parsed.author), "%s", view.author);
    std::snprintf(parsed.description, sizeof(parsed.description), "%s", view.description);
    std::snprintf(parsed.action_label, sizeof(parsed.action_label), "%s", view.action_label);
    std::snprintf(parsed.status, sizeof(parsed.status), "%s", view.status);
    if (view.type == DmModType::Action) {
        if (view.action_label[0] == '\0') return false;
        parsed.type = ModType::Action;
    } else if (view.type == DmModType::Toggle) {
        parsed.type = ModType::Toggle;
    } else {
        return false;
    }
    parsed.enabled = view.runtime_enabled != FALSE;
    parsed.action_applied = view.state == DmModState::ActionCompleted;

    for (std::uint32_t index = 0; index < view.option_count; ++index) {
        const DmModOptionView& source = view.options[index];
        if (source.struct_size != sizeof(DmModOptionView) ||
            source.value.struct_size != sizeof(DmModValue) ||
            source.type != source.value.type) {
            return false;
        }
        ModOption option = {};
        std::snprintf(option.id, sizeof(option.id), "%s", source.id);
        std::snprintf(option.name, sizeof(option.name), "%s", source.name);
        if (source.type == DmOptionType::SliderInt) {
            option.type = OptionType::SliderInt;
            option.int_val = source.value.int_value;
            option.min_int = source.min_int;
            option.max_int = source.max_int;
        } else if (source.type == DmOptionType::SliderFloat) {
            option.type = OptionType::SliderFloat;
            option.float_val = source.value.float_value;
            option.min_float = source.min_float;
            option.max_float = source.max_float;
        } else if (source.type == DmOptionType::Toggle) {
            option.type = OptionType::Toggle;
            option.bool_val = source.value.bool_value != FALSE;
        } else {
            return false;
        }
        parsed.options.push_back(option);
    }
    mod = std::move(parsed);
    return true;
}

void RefreshModFromLoader(ModItem& mod) {
    if (g_loader_api == nullptr || g_loader_api->GetModById == nullptr) return;
    DmModView view = {};
    view.struct_size = sizeof(view);
    if (g_loader_api->GetModById(mod.id, &view) != FALSE && !CopyLoaderView(view, mod)) {
        std::snprintf(mod.status, sizeof(mod.status), "ERRO: dados invalidos recebidos do loader.");
    }
}

void ExecuteModActionGeneric(ModItem& mod) {
    if (g_loader_api == nullptr || g_loader_api->ExecuteModAction == nullptr ||
        g_loader_api->ExecuteModAction(mod.id) == FALSE) {
        std::snprintf(mod.status, sizeof(mod.status), "ERRO: loader rejeitou a action.");
    }
    RefreshModFromLoader(mod);
}

void NotifyModToggle(ModItem& mod) {
    if (g_loader_api == nullptr || g_loader_api->SetModEnabled == nullptr ||
        g_loader_api->SetModEnabled(mod.id, mod.enabled ? TRUE : FALSE) == FALSE) {
        std::snprintf(mod.status, sizeof(mod.status), "ERRO: transicao de ciclo de vida rejeitada.");
    }
    RefreshModFromLoader(mod);
}

void ApplyModOption(ModItem& mod, const ModOption& option) {
    if (g_loader_api == nullptr || g_loader_api->SetModOption == nullptr) {
        std::snprintf(mod.status, sizeof(mod.status), "ERRO: API do loader indisponivel.");
        return;
    }
    DmModValue value = {};
    value.struct_size = sizeof(value);
    if (option.type == OptionType::SliderInt) {
        value.type = DmOptionType::SliderInt;
        value.int_value = option.int_val;
    } else if (option.type == OptionType::SliderFloat) {
        value.type = DmOptionType::SliderFloat;
        value.float_value = option.float_val;
    } else {
        value.type = DmOptionType::Toggle;
        value.bool_value = option.bool_val ? TRUE : FALSE;
    }
    if (g_loader_api->SetModOption(mod.id, option.id, &value) == FALSE) {
        std::snprintf(mod.status, sizeof(mod.status), "ERRO: opcao rejeitada pelo mod.");
    }
}

void CommitModOptions(ModItem& mod) {
    if (g_loader_api != nullptr && g_loader_api->FlushModConfig != nullptr &&
        g_loader_api->FlushModConfig(mod.id) == FALSE) {
        std::snprintf(mod.status, sizeof(mod.status), "ERRO: falha ao gravar config.json.");
    }
    RefreshModFromLoader(mod);
}

void NotifyModOptionChanged(ModItem& mod, const ModOption& option) {
    ApplyModOption(mod, option);
    CommitModOptions(mod);
}

void FlushAllModConfigs() {
    if (g_loader_api != nullptr && g_loader_api->FlushModConfig != nullptr) {
        g_loader_api->FlushModConfig(nullptr);
    }
}

void ScanAndDiscoverMods() {
    g_discovered_mods.clear();
    if (g_loader_api == nullptr || g_loader_api->GetModCount == nullptr || g_loader_api->GetMod == nullptr) {
        g_mods_scanned = true;
        return;
    }

    const std::uint32_t count = g_loader_api->GetModCount();
    for (std::uint32_t index = 0; index < count; ++index) {
        DmModView view = {};
        view.struct_size = sizeof(view);
        if (g_loader_api->GetMod(index, &view) == FALSE || view.type == DmModType::System) continue;
        ModItem mod = {};
        if (CopyLoaderView(view, mod)) {
            g_discovered_mods.push_back(std::move(mod));
        }
    }
    g_mods_scanned = true;
    if (g_selected_mod >= static_cast<int>(g_discovered_mods.size())) g_selected_mod = 0;
}

enum class ActiveFocusPanel {
    LeftList,
    RightOptions
};

struct GamepadNavState {
    DWORD last_buttons = 0;
    ActiveFocusPanel active_panel = ActiveFocusPanel::LeftList;
    int focused_option = 0; // 0 = main action/toggle, 1..N = mod options (1-indexed)
    float right_stick_scroll = 0.0f;
    bool last_max = false;
    bool last_min = false;
};
static GamepadNavState g_gp_nav = {};

void ProcessGamepadNavigation(float /*dt*/) {
    if (!g_overlay_visible || g_discovered_mods.empty()) {
        g_gp_nav.last_buttons = 0;
        g_gp_nav.last_max = false;
        g_gp_nav.last_min = false;
        return;
    }

    const WORD buttons = g_input.pad_buttons;
    const SHORT lx = g_input.lx;
    const SHORT ly = g_input.ly;
    const SHORT ry = g_input.ry;

    g_gp_nav.right_stick_scroll = (std::abs(ry) > 6000) ? (static_cast<float>(ry) / 32768.0f) : 0.0f;

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

    const bool max_down = g_input.pad_max || g_input.key_max;
    const bool min_down = g_input.pad_min || g_input.key_min;
    const bool want_max = max_down && !g_gp_nav.last_max;
    const bool want_min = min_down && !g_gp_nav.last_min;
    g_gp_nav.last_max = max_down;
    g_gp_nav.last_min = min_down;

    // LB / RB: Toggle focus between Left List and Right Options
    if ((lb && !last_lb) || (rb && !last_rb)) {
        if (g_gp_nav.active_panel == ActiveFocusPanel::LeftList) {
            g_gp_nav.active_panel = ActiveFocusPanel::RightOptions;
            g_gp_nav.focused_option = 0;
        } else {
            g_gp_nav.active_panel = ActiveFocusPanel::LeftList;
        }
    }

    if (g_gp_nav.active_panel == ActiveFocusPanel::LeftList) {
        // --- Left Panel Navigation (Mod List) ---
        if (up && !last_up) {
            if (g_selected_mod > 0) {
                g_selected_mod--;
            } else {
                g_selected_mod = static_cast<int>(g_discovered_mods.size()) - 1;
            }
            g_gp_nav.focused_option = 0;
        }
        if (down && !last_down) {
            if (g_selected_mod + 1 < static_cast<int>(g_discovered_mods.size())) {
                g_selected_mod++;
            } else {
                g_selected_mod = 0;
            }
            g_gp_nav.focused_option = 0;
        }
        // Right or A button switches focus to options panel
        if ((right && !last_right) || (a_btn && !last_a)) {
            g_gp_nav.active_panel = ActiveFocusPanel::RightOptions;
            g_gp_nav.focused_option = 0;
        }
    } else {
        // --- Right Panel Navigation (Options & Sliders) ---
        if (g_selected_mod >= 0 && g_selected_mod < static_cast<int>(g_discovered_mods.size())) {
            auto& mod = g_discovered_mods[g_selected_mod];
            const int total_items = 1 + static_cast<int>(mod.options.size());

            // Move focus Up/Down between options
            if (up && !last_up) {
                if (g_gp_nav.focused_option > 0) {
                    g_gp_nav.focused_option--;
                } else {
                    // At top item, go back to left list
                    g_gp_nav.active_panel = ActiveFocusPanel::LeftList;
                }
            }
            if (down && !last_down) {
                if (g_gp_nav.focused_option + 1 < total_items) {
                    g_gp_nav.focused_option++;
                }
            }

            // Left when on main item returns focus to left panel
            if (g_gp_nav.focused_option == 0 && left && !last_left) {
                g_gp_nav.active_panel = ActiveFocusPanel::LeftList;
            }

            // Interact with focused item
            if (g_gp_nav.focused_option == 0) {
                // Focus is on Main Control (Toggle or Action)
                if (a_btn && !last_a) {
                    if (mod.type == ModType::Toggle) {
                        mod.enabled = !mod.enabled;
                        NotifyModToggle(mod);
                    } else if (mod.type == ModType::Action) {
                        ExecuteModActionGeneric(mod);
                    }
                }
            } else {
                // Focus is on sub-option (1..N)
                const size_t opt_idx = static_cast<size_t>(g_gp_nav.focused_option - 1);
                if (opt_idx < mod.options.size()) {
                    auto& opt = mod.options[opt_idx];
                    const int step_int = (opt.max_int - opt.min_int > 50) ? 5 : 1;
                    const float span = opt.max_float - opt.min_float;
                    const float step_float = span > 50.0f ? span / 100.0f : 0.5f;
                    bool changed = false;

                    if (opt.type == OptionType::SliderInt) {
                        if (left && !last_left) {
                            opt.int_val = std::max(opt.min_int, opt.int_val - step_int);
                            changed = true;
                        }
                        if (right && !last_right) {
                            opt.int_val = std::min(opt.max_int, opt.int_val + step_int);
                            changed = true;
                        }
                        if (a_btn && !last_a) {
                            opt.int_val += step_int;
                            if (opt.int_val > opt.max_int) opt.int_val = opt.min_int;
                            changed = true;
                        }
                        if (want_max) { opt.int_val = opt.max_int; changed = true; }
                        if (want_min) { opt.int_val = opt.min_int; changed = true; }
                    } else if (opt.type == OptionType::SliderFloat) {
                        if (left && !last_left) {
                            opt.float_val = std::max(opt.min_float, opt.float_val - step_float);
                            changed = true;
                        }
                        if (right && !last_right) {
                            opt.float_val = std::min(opt.max_float, opt.float_val + step_float);
                            changed = true;
                        }
                        if (want_max) { opt.float_val = opt.max_float; changed = true; }
                        if (want_min) { opt.float_val = opt.min_float; changed = true; }
                    } else if (opt.type == OptionType::Toggle) {
                        if ((left && !last_left) || (right && !last_right) || (a_btn && !last_a)) {
                            opt.bool_val = !opt.bool_val;
                            changed = true;
                        }
                        if (want_max) { opt.bool_val = true; changed = true; }
                        if (want_min) { opt.bool_val = false; changed = true; }
                    }

                    if (changed) {
                        NotifyModOptionChanged(mod, opt);
                    }
                }
            }
        }
    }

    g_gp_nav.last_buttons = buttons;
}

void UpdateGamepadIO(float dt) {
    if (!g_overlay_visible) return;
    ProcessGamepadNavigation(dt);
}

void BuildOverlay(const ImVec2& display_size) {
    if (!g_mods_scanned) {
        ScanAndDiscoverMods();
    }

    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0.0f, 0.0f), display_size, IM_COL32(7, 5, 12, 175));

    const float ui_scale = std::clamp(
        std::min(display_size.x / 1920.0f, display_size.y / 1080.0f), 0.78f, 1.60f);
    const float margin = std::max(8.0f, 16.0f * ui_scale);
    const float available_width = std::max(1.0f, display_size.x - margin * 2.0f);
    const float available_height = std::max(1.0f, display_size.y - margin * 2.0f);
    const float minimum_width = std::min(640.0f * ui_scale, available_width);
    const float maximum_width = std::min(1440.0f * ui_scale, available_width);
    const float minimum_height = std::min(480.0f * ui_scale, available_height);
    const float maximum_height = std::min(840.0f * ui_scale, available_height);
    const float width = std::clamp(display_size.x * 0.84f, minimum_width, maximum_width);
    const float height = std::clamp(display_size.y * 0.82f, minimum_height, maximum_height);
    const bool wide_layout = width >= 900.0f * ui_scale;

    ImGui::SetNextWindowPos(
        ImVec2((display_size.x - width) * 0.5f, (display_size.y - height) * 0.5f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f * ui_scale, 18.0f * ui_scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(9.0f * ui_scale, 9.0f * ui_scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * ui_scale, 5.0f * ui_scale));
    ImGui::Begin(
        "##DisgaeaMayhemModManager",
        nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetWindowFontScale(ui_scale);

    // Header
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.74f, 0.20f, 1.0f));
    ImGui::SetWindowFontScale(ui_scale * 1.20f);
    ImGui::TextWrapped("DISGAEA MAYHEM - MOD MANAGER (IN-GAME)");
    ImGui::SetWindowFontScale(ui_scale);
    ImGui::PopStyleColor();
    const char* engine_status = "DirectX 12 Engine: ATIVO";
    const float engine_status_width = ImGui::CalcTextSize(engine_status).x;
    if (engine_status_width <= ImGui::GetContentRegionAvail().x) {
        ImGui::SetCursorPosX(std::max(
            ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - engine_status_width));
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.85f, 0.4f, 1.0f));
    ImGui::TextWrapped("%s", engine_status);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 2.0f * ui_scale));

    // Layout em colunas quando houver espaco; em telas menores, os paineis ficam empilhados.
    const float footer_height = 72.0f * ui_scale;
    const float content_height = std::max(
        140.0f * ui_scale, ImGui::GetContentRegionAvail().y - footer_height);
    const float row_height = 32.0f * ui_scale;
    const float left_width = wide_layout
        ? std::clamp(width * 0.29f, 220.0f * ui_scale, 360.0f * ui_scale)
        : 0.0f;
    const float list_height = wide_layout
        ? content_height
        : std::min(
              content_height * 0.38f,
              std::max(130.0f * ui_scale,
                       (static_cast<float>(g_discovered_mods.size()) + 1.8f) * row_height));
    const bool is_left_focused = (g_gp_nav.active_panel == ActiveFocusPanel::LeftList);

    // --- Left Panel: Dynamic Mod List ---
    if (is_left_focused) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.74f, 0.20f, 0.95f));
    }
    ImGui::BeginChild("##ModListPanel", ImVec2(left_width, list_height), true);
    ImGui::SetWindowFontScale(ui_scale);
    if (is_left_focused) {
        ImGui::PopStyleColor();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "MODS (%d)", static_cast<int>(g_discovered_mods.size()));
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "MODS (%d)", static_cast<int>(g_discovered_mods.size()));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Atualizar")) {
        ScanAndDiscoverMods();
    }
    if (is_left_focused) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Foco: lista");
    }
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    for (size_t i = 0; i < g_discovered_mods.size(); ++i) {
        auto& mod = g_discovered_mods[i];
        ImGui::PushID(static_cast<int>(i));
        const bool is_selected = (g_selected_mod == static_cast<int>(i));
        
        char label[128];
        if (mod.type == ModType::Action) {
            std::snprintf(label, sizeof(label), "%s%s %s",
                (is_selected && is_left_focused) ? "> " : "  ",
                mod.action_applied ? "[OK]" : "[*]",
                mod.name);
        } else {
            std::snprintf(label, sizeof(label), "%s%s %s",
                (is_selected && is_left_focused) ? "> " : "  ",
                mod.enabled ? "[ON]" : "[OFF]",
                mod.name);
        }

        if (is_selected && is_left_focused) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.25f, 0.12f, 0.95f));
        }

        const ImVec2 row_start = ImGui::GetCursorScreenPos();
        const float row_wrap_width = std::max(
            80.0f * ui_scale, ImGui::GetContentRegionAvail().x - 12.0f * ui_scale);
        const float label_height = ImGui::CalcTextSize(
            label, nullptr, false, row_wrap_width).y;
        const float selectable_height = std::max(
            row_height, label_height + ImGui::GetStyle().FramePadding.y * 2.0f);

        if (ImGui::Selectable("##mod_row", is_selected, 0, ImVec2(0, selectable_height))) {
            g_selected_mod = static_cast<int>(i);
            g_gp_nav.active_panel = ActiveFocusPanel::LeftList;
        }

        if (is_selected && is_left_focused) {
            ImGui::PopStyleColor();
            ImGui::SetItemDefaultFocus();
        }

        ImGui::GetWindowDrawList()->AddText(
            ImGui::GetFont(),
            ImGui::GetFontSize(),
            ImVec2(
                row_start.x + ImGui::GetStyle().FramePadding.x,
                row_start.y + ImGui::GetStyle().FramePadding.y),
            ImGui::GetColorU32(ImGuiCol_Text),
            label,
            nullptr,
            row_wrap_width);

        ImGui::PopID();
    }
    ImGui::EndChild();

    if (wide_layout) {
        ImGui::SameLine();
    } else {
        ImGui::Dummy(ImVec2(0.0f, 3.0f * ui_scale));
    }

    // --- Right Panel: Selected Mod Dynamic UI & Options ---
    const bool is_right_focused = (g_gp_nav.active_panel == ActiveFocusPanel::RightOptions);
    if (is_right_focused) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.74f, 0.20f, 0.95f));
    }
    const float details_height = wide_layout
        ? content_height
        : std::max(100.0f * ui_scale,
                   content_height - list_height - 3.0f * ui_scale);
    ImGui::BeginChild(
        "##ModDetailsPanel", ImVec2(0.0f, details_height), true,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::SetWindowFontScale(ui_scale);
    if (is_right_focused) {
        ImGui::PopStyleColor();
    }

    // Right stick smooth scrolling
    if (std::abs(g_gp_nav.right_stick_scroll) > 0.05f) {
        float cur_scroll = ImGui::GetScrollY();
        ImGui::SetScrollY(cur_scroll - g_gp_nav.right_stick_scroll * 10.0f);
    }

    if (g_selected_mod >= 0 && g_selected_mod < static_cast<int>(g_discovered_mods.size())) {
        auto& mod = g_discovered_mods[g_selected_mod];

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.82f, 0.3f, 1.0f));
        ImGui::SetWindowFontScale(ui_scale * 1.15f);
        ImGui::TextWrapped("%s", mod.name);
        ImGui::SetWindowFontScale(ui_scale);
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        ImGui::TextWrapped("Categoria: %s", mod.category);
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.65f, 1.0f));
        ImGui::TextWrapped("Versao: %s", mod.version);
        ImGui::TextWrapped("Autor: %s", mod.author);
        ImGui::PopStyleColor();
        if (is_right_focused) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
            ImGui::TextWrapped("Foco: opcoes - LB/RB para voltar a lista");
            ImGui::PopStyleColor();
        }
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Descricao:");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.85f, 1.0f));
        ImGui::TextWrapped("%s", mod.description);
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        // --- Main Mod Control (Option Index 0) ---
        const bool is_opt0_focused = (is_right_focused && g_gp_nav.focused_option == 0);
        if (is_opt0_focused) {
            ImGui::SetScrollHereY(0.2f);
        }

        if (mod.type == ModType::Toggle) {
            if (is_opt0_focused) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "> Controle Principal:");
            } else {
                ImGui::TextUnformatted("  Controle Principal:");
            }
            if (mod.enabled) {
                ImGui::PushStyleColor(ImGuiCol_Button, is_opt0_focused ? ImVec4(0.25f, 0.85f, 0.35f, 1.0f) : ImVec4(0.15f, 0.65f, 0.25f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.75f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.85f, 0.35f, 1.0f));
                if (ImGui::Button(
                        is_opt0_focused ? " > [ ATIVADO (ON) ] < " : "  [ ATIVADO (ON) ]  ",
                        ImVec2(180.0f * ui_scale, 34.0f * ui_scale))) {
                    mod.enabled = false;
                    NotifyModToggle(mod);
                    g_gp_nav.active_panel = ActiveFocusPanel::RightOptions;
                    g_gp_nav.focused_option = 0;
                }
                ImGui::PopStyleColor(3);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, is_opt0_focused ? ImVec4(0.55f, 0.55f, 0.55f, 1.0f) : ImVec4(0.35f, 0.35f, 0.35f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
                if (ImGui::Button(
                        is_opt0_focused ? " > [ DESATIVADO (OFF) ] < " : "  [ DESATIVADO (OFF) ]  ",
                        ImVec2(180.0f * ui_scale, 34.0f * ui_scale))) {
                    mod.enabled = true;
                    NotifyModToggle(mod);
                    g_gp_nav.active_panel = ActiveFocusPanel::RightOptions;
                    g_gp_nav.focused_option = 0;
                }
                ImGui::PopStyleColor(3);
            }
        } else if (mod.type == ModType::Action) {
            if (is_opt0_focused) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "> Acao do Mod:");
            } else {
                ImGui::TextUnformatted("  Acao do Mod:");
            }
            char btn_lbl[64];
            std::snprintf(btn_lbl, sizeof(btn_lbl),
                is_opt0_focused ? " > [ %s ] < " : "  [ %s ]  ", mod.action_label);

            ImGui::PushStyleColor(ImGuiCol_Button, mod.action_applied ? ImVec4(0.18f, 0.45f, 0.25f, 0.95f) : ImVec4(0.72f, 0.25f, 0.15f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.35f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.95f, 0.45f, 0.25f, 1.0f));

            const float action_button_width = std::min(
                ImGui::GetContentRegionAvail().x,
                std::max(
                    180.0f * ui_scale,
                    ImGui::CalcTextSize(btn_lbl).x + ImGui::GetStyle().FramePadding.x * 2.0f));
            if (ImGui::Button(btn_lbl, ImVec2(action_button_width, 34.0f * ui_scale))) {
                ExecuteModActionGeneric(mod);
                g_gp_nav.active_panel = ActiveFocusPanel::RightOptions;
                g_gp_nav.focused_option = 0;
            }
            ImGui::PopStyleColor(3);
        }

        // --- Dynamic Sub-Options / Sliders (Option Indices 1..N) ---
        if (!mod.options.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Opcoes & Parametros:");
            ImGui::Dummy(ImVec2(0.0f, 2.0f));

            for (size_t j = 0; j < mod.options.size(); ++j) {
                auto& opt = mod.options[j];
                const bool is_cur_opt_focused = (is_right_focused && g_gp_nav.focused_option == static_cast<int>(j + 1));
                
                ImGui::PushID(static_cast<int>(j));

                if (is_cur_opt_focused) {
                    ImGui::SetScrollHereY(0.45f);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.35f, 0.25f, 0.12f, 0.95f));
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.85f, 0.2f, 1.0f));
                }

                if (opt.type == OptionType::Toggle) {
                    if (ImGui::Checkbox("##toggle", &opt.bool_val)) {
                        NotifyModOptionChanged(mod, opt);
                        g_gp_nav.active_panel = ActiveFocusPanel::RightOptions;
                        g_gp_nav.focused_option = static_cast<int>(j + 1);
                    }
                    ImGui::SameLine();
                    if (is_cur_opt_focused) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.2f, 1.0f));
                    }
                    ImGui::TextWrapped("%s%s", is_cur_opt_focused ? "> " : "", opt.name);
                    if (is_cur_opt_focused) {
                        ImGui::PopStyleColor();
                    }
                } else if (opt.type == OptionType::SliderInt) {
                    if (is_cur_opt_focused) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.2f, 1.0f));
                        ImGui::TextWrapped("> %s  [ < / > para ajustar ]", opt.name);
                        ImGui::PopStyleColor();
                    } else {
                        ImGui::TextWrapped("%s", opt.name);
                    }
                    ImGui::PushItemWidth(std::max(
                        150.0f * ui_scale,
                        ImGui::GetContentRegionAvail().x - 20.0f * ui_scale));
                    if (ImGui::SliderInt("##slider", &opt.int_val, opt.min_int, opt.max_int)) {
                        ApplyModOption(mod, opt);
                        g_gp_nav.active_panel = ActiveFocusPanel::RightOptions;
                        g_gp_nav.focused_option = static_cast<int>(j + 1);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        CommitModOptions(mod);
                    }
                    ImGui::PopItemWidth();
                } else if (opt.type == OptionType::SliderFloat) {
                    if (is_cur_opt_focused) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.2f, 1.0f));
                        ImGui::TextWrapped("> %s  [ < / > para ajustar ]", opt.name);
                        ImGui::PopStyleColor();
                    } else {
                        ImGui::TextWrapped("%s", opt.name);
                    }
                    ImGui::PushItemWidth(std::max(
                        150.0f * ui_scale,
                        ImGui::GetContentRegionAvail().x - 20.0f * ui_scale));
                    if (ImGui::SliderFloat("##slider", &opt.float_val, opt.min_float, opt.max_float, "%.1f")) {
                        ApplyModOption(mod, opt);
                        g_gp_nav.active_panel = ActiveFocusPanel::RightOptions;
                        g_gp_nav.focused_option = static_cast<int>(j + 1);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        CommitModOptions(mod);
                    }
                    ImGui::PopItemWidth();
                }

                if (is_cur_opt_focused) {
                    ImGui::PopStyleColor(2);
                }

                ImGui::PopID();
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::Separator();

        // Status Line
        if (mod.type == ModType::Toggle) {
            if (mod.enabled) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.95f, 0.4f, 1.0f));
                ImGui::TextWrapped("Status: Ativo [ON] - %s", mod.status);
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                ImGui::TextWrapped("Status: Desativado [OFF] - %s", mod.status);
                ImGui::PopStyleColor();
            }
        } else {
            if (mod.action_applied) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.95f, 0.4f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
            }
            ImGui::TextWrapped("Status: %s", mod.status);
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    // Footer
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.74f, 0.20f, 1.0f));
    ImGui::TextWrapped(
        "Abrir: Select / L3+R3 / F1  |  Painel: LB/RB  |  D-Pad: navegar  |  A: confirmar"
        "  |  Maximo: RT / Home  |  Minimo: LT / End");
    ImGui::PopStyleColor();
    if (ImGui::SmallButton("Voltar ao jogo (B / Esc)")) {
        CloseOverlay();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

bool RenderFrame() {
    UpdateOverlayState();
    if (!g_overlay_visible.load(std::memory_order_acquire)) {
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
    io.MouseDrawCursor = true;
    UpdateGamepadIO(io.DeltaTime);

    ImGui_ImplDX12_NewFrame();
    ImGui::SetNextFrameWantCaptureMouse(true);
    ImGui::SetNextFrameWantCaptureKeyboard(true);
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
        if (g_shared.status != STATUS_FAILED) {
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
    if (!CreateNmplInputHooks()) {
        return false;
    }
    if (!FindHookTargets()) {
        RemoveNmplInputHooks();
        return false;
    }
    if (!dm::CreateHook(g_present_target, reinterpret_cast<void*>(&HookPresent),
                        reinterpret_cast<void**>(&g_original_present))) {
        SetFailure(1602, "Could not create the IDXGISwapChain::Present hook");
        RemoveNmplInputHooks();
        return false;
    }
    if (!dm::CreateHook(g_resize_buffers_target, reinterpret_cast<void*>(&HookResizeBuffers),
                        reinterpret_cast<void**>(&g_original_resize_buffers))) {
        SetFailure(1603, "Could not create the IDXGISwapChain::ResizeBuffers hook");
        dm::RemoveHook(g_present_target);
        RemoveNmplInputHooks();
        return false;
    }
    if (!dm::CreateHook(g_execute_target, reinterpret_cast<void*>(&HookExecuteCommandLists),
                        reinterpret_cast<void**>(&g_original_execute_command_lists))) {
        SetFailure(1604, "Could not create the ID3D12CommandQueue::ExecuteCommandLists hook");
        dm::RemoveHook(g_resize_buffers_target);
        dm::RemoveHook(g_present_target);
        RemoveNmplInputHooks();
        return false;
    }
    if (!QueueEnableNmplInputHooks() ||
        !dm::QueueHook(g_present_target, true) ||
        !dm::QueueHook(g_resize_buffers_target, true) ||
        !dm::QueueHook(g_execute_target, true) ||
        !dm::ApplyHooks()) {
        SetFailure(1605, "Could not enable the DirectX 12 and exclusive-input hooks");
        dm::RemoveHook(g_execute_target);
        dm::RemoveHook(g_resize_buffers_target);
        dm::RemoveHook(g_present_target);
        RemoveNmplInputHooks();
        return false;
    }
    return true;
}

void RemoveHooks() {
    g_shutting_down.store(true, std::memory_order_release);
    if (g_present_target != nullptr) dm::QueueHook(g_present_target, false);
    if (g_resize_buffers_target != nullptr) dm::QueueHook(g_resize_buffers_target, false);
    if (g_execute_target != nullptr) dm::QueueHook(g_execute_target, false);
    DisableNmplInputHooks();
    dm::ApplyHooks();
    for (int attempt = 0;
         attempt < 5000 && g_active_hooks.load(std::memory_order_acquire) != 0;
         ++attempt) {
        Sleep(1);
    }
    dm::RemoveHook(g_execute_target);
    dm::RemoveHook(g_resize_buffers_target);
    dm::RemoveHook(g_present_target);
    RemoveNmplInputHooks();
}



}  // namespace

extern "C" __declspec(dllexport) std::uint32_t WINAPI Mod_GetAbiVersion() {
    return DM_MOD_LOADER_ABI_VERSION;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Initialize(const DmModHostContext* context) {
    if (!dm::AcceptHostContext(context, "mod_menu", false)) return FALSE;
    g_loader_api = context->loader;
    InterlockedExchange(&g_shared.status, STATUS_WAITING);
    InterlockedExchange(&g_shared.error_code, 0);
    g_shared.error_message[0] = '\0';
    ScanAndDiscoverMods();
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Enable() {
    if (g_hooks_installed.load(std::memory_order_acquire)) return TRUE;
    g_shutting_down.store(false, std::memory_order_release);
    if (!CreateHooks()) {
        if (g_loader_api != nullptr && g_loader_api->Log != nullptr) {
            g_loader_api->Log("mod_menu", g_shared.error_message);
        }
        return FALSE;
    }
    g_hooks_installed.store(true, std::memory_order_release);
    SetStatus(STATUS_HOOKS_INSTALLED);
    if (g_loader_api != nullptr && g_loader_api->Log != nullptr) {
        g_loader_api->Log("mod_menu", "Interface DirectX 12 e bloqueio de comandos instalados.");
    }
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Disable() {
    if (!g_hooks_installed.exchange(false, std::memory_order_acq_rel)) return TRUE;
    RemoveHooks();
    if (g_active_hooks.load(std::memory_order_acquire) != 0) {
        SetFailure(1701, "Timeout ao encerrar hooks DirectX ativos.");
        return FALSE;
    }
    AcquireSRWLockExclusive(&g_lock);
    DestroyRenderer();
    SafeRelease(g_queue);
    SetStatus(STATUS_WAITING);
    ReleaseSRWLockExclusive(&g_lock);
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_SetOption(const char*, const DmModValue*) {
    return FALSE;
}

extern "C" __declspec(dllexport) void WINAPI Mod_Shutdown() {
    Mod_Disable();
    g_loader_api = nullptr;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
