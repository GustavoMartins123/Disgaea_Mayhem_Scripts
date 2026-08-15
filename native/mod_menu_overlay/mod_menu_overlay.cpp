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

#include "MinHook.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"

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
SharedState* g_shared = nullptr;
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

bool ControllerBDown() {
    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
        XINPUT_STATE state = {};
        const DWORD result = XInputGetState(index, &state);
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
    ImFontConfig font_config;
    font_config.SizePixels = 22.0f;
    if (io.Fonts->AddFontDefault(&font_config) == nullptr) {
        SetFailure(1207, "Could not create the Mod Manager font");
        DestroyRenderer();
        return false;
    }

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.WindowPadding = ImVec2(30.0f, 26.0f);
    style.ItemSpacing = ImVec2(10.0f, 14.0f);
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
    if (g_shared->open_request != 0) {
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

void BuildOverlay(const ImVec2& display_size) {
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0.0f, 0.0f), display_size, IM_COL32(7, 5, 12, 155));

    const float width = std::min(820.0f, std::max(520.0f, display_size.x - 160.0f));
    const float height = std::min(430.0f, std::max(330.0f, display_size.y - 180.0f));
    ImGui::SetNextWindowPos(
        ImVec2((display_size.x - width) * 0.5f, (display_size.y - height) * 0.5f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::Begin(
        "##DisgaeaMayhemModManager",
        nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoNav);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.74f, 0.20f, 1.0f));
    ImGui::SetWindowFontScale(1.65f);
    ImGui::TextUnformatted("MOD MANAGER");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 20.0f));
    ImGui::SetWindowFontScale(1.2f);
    ImGui::TextUnformatted("Nenhum mod nativo registrado.");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped(
        "O item Mods esta conectado ao carregador nativo. Os modulos serao "
        "listados aqui quando implementarem a ABI do Mod Manager.");
    ImGui::PopStyleColor();

    const float prompt_y = height - 82.0f;
    if (ImGui::GetCursorPosY() < prompt_y) {
        ImGui::SetCursorPosY(prompt_y);
    }
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.74f, 0.20f, 1.0f));
    ImGui::TextUnformatted("B / Esc");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted("Voltar");
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

}  // namespace

extern "C" __declspec(dllexport) DWORD WINAPI InitializeModMenu(void* parameter) {
    if (parameter == nullptr || g_shared != nullptr) {
        return 0;
    }
    g_shared = static_cast<SharedState*>(parameter);
    if (!IsWritableAddress(g_shared, sizeof(SharedState))) {
        g_shared = nullptr;
        return 0;
    }
    InterlockedExchange(&g_shared->status, STATUS_WAITING);
    InterlockedExchange(&g_shared->error_code, 0);
    g_shared->error_message[0] = '\0';
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
    g_shared = nullptr;
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
