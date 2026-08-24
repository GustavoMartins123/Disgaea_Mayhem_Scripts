#pragma once

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "mod_loader_api.h"

namespace dm {

struct GameImage {
    std::uintptr_t base = 0;
    std::size_t size = 0;
    bool verified = false;

    bool Contains(std::uintptr_t address, std::size_t length) const {
        return base != 0 && address >= base && length <= size &&
               address - base <= size - length;
    }
};

inline GameImage g_game_image;

struct RegionInfo {
    std::uintptr_t base = 0;
    std::size_t size = 0;
    DWORD protect = 0;
};

inline thread_local RegionInfo t_region_cache;

inline bool ProtectAllows(DWORD protect, bool require_write) {
    const DWORD masked = protect & 0xFF;
    if (require_write) {
        return masked == PAGE_READWRITE || masked == PAGE_WRITECOPY ||
               masked == PAGE_EXECUTE_READWRITE || masked == PAGE_EXECUTE_WRITECOPY;
    }
    return masked == PAGE_READONLY || masked == PAGE_READWRITE || masked == PAGE_WRITECOPY ||
           masked == PAGE_EXECUTE_READ || masked == PAGE_EXECUTE_READWRITE ||
           masked == PAGE_EXECUTE_WRITECOPY;
}

inline bool QueryRegion(std::uintptr_t address, RegionInfo& out) {
    MEMORY_BASIC_INFORMATION information = {};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &information,
                     sizeof(information)) != sizeof(information) ||
        information.State != MEM_COMMIT ||
        (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }
    out.base = reinterpret_cast<std::uintptr_t>(information.BaseAddress);
    out.size = information.RegionSize;
    out.protect = information.Protect;
    return true;
}

inline bool IsAccessibleRange(const void* address, std::size_t size, bool require_write) {
    if (address == nullptr || size == 0) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    if (start > std::numeric_limits<std::uintptr_t>::max() - size) return false;

    const RegionInfo& cached = t_region_cache;
    if (cached.base != 0 && start >= cached.base && start - cached.base < cached.size &&
        size <= cached.size - (start - cached.base)) {
        if (ProtectAllows(cached.protect, require_write)) return true;
    }

    RegionInfo region;
    if (!QueryRegion(start, region)) return false;
    t_region_cache = region;
    return start - region.base < region.size &&
           size <= region.size - (start - region.base) &&
           ProtectAllows(region.protect, require_write);
}

inline bool IsReadableRange(const void* address, std::size_t size) {
    return IsAccessibleRange(address, size, false);
}

inline bool IsWritableRange(const void* address, std::size_t size) {
    return IsAccessibleRange(address, size, true);
}

inline bool IsExecutableAddress(const void* address) {
    if (address == nullptr) return false;
    RegionInfo region;
    if (!QueryRegion(reinterpret_cast<std::uintptr_t>(address), region)) return false;
    const DWORD masked = region.protect & 0xFF;
    return masked == PAGE_EXECUTE || masked == PAGE_EXECUTE_READ ||
           masked == PAGE_EXECUTE_READWRITE || masked == PAGE_EXECUTE_WRITECOPY;
}

class CallGuard {
public:
    explicit CallGuard(std::atomic<LONG>& counter) : counter_(counter) {
        counter_.fetch_add(1, std::memory_order_acq_rel);
    }
    ~CallGuard() { counter_.fetch_sub(1, std::memory_order_acq_rel); }
    CallGuard(const CallGuard&) = delete;
    CallGuard& operator=(const CallGuard&) = delete;

private:
    std::atomic<LONG>& counter_;
};

inline void DrainActiveCalls(const std::atomic<LONG>& counter) {
    while (counter.load(std::memory_order_acquire) != 0) SwitchToThread();
}

inline const DmModLoaderApi* g_loader = nullptr;
inline const char* g_mod_id = "plugin";

class HostLog {
public:
    void Bind(const DmModLoaderApi* loader, const char* component) {
        loader_ = loader;
        component_ = component;
    }
    void Reset() { loader_ = nullptr; }
    void operator()(const char* message) const {
        if (loader_ != nullptr && loader_->Log != nullptr) loader_->Log(component_, message);
    }

private:
    const DmModLoaderApi* loader_ = nullptr;
    const char* component_ = "plugin";
};

inline bool AcceptHostContext(const DmModHostContext* context, const char* mod_id,
                              bool require_verified_build) {
    if (context == nullptr || context->struct_size != sizeof(DmModHostContext) ||
        context->abi_version != DM_MOD_LOADER_ABI_VERSION || context->loader == nullptr ||
        context->loader->struct_size != sizeof(DmModLoaderApi) ||
        context->loader->abi_version != DM_MOD_LOADER_ABI_VERSION ||
        context->loader->CreateHook == nullptr || context->loader->QueueHook == nullptr ||
        context->loader->ApplyHooks == nullptr || context->loader->RemoveHook == nullptr) {
        return false;
    }
    if (context->game_module_base == 0) return false;
    if (require_verified_build && context->game_build_verified == FALSE) return false;

    g_loader = context->loader;
    g_mod_id = mod_id;
    g_game_image.base = context->game_module_base;
    g_game_image.size = context->game_module_size;
    g_game_image.verified = context->game_build_verified != FALSE;
    return true;
}

inline void ReleaseHost() { g_loader = nullptr; }

inline bool CreateHook(void* target, void* detour, void** original) {
    return g_loader != nullptr &&
           g_loader->CreateHook(g_mod_id, target, detour, original) != FALSE;
}

inline bool QueueHook(void* target, bool enabled) {
    return g_loader != nullptr &&
           g_loader->QueueHook(g_mod_id, target, enabled ? TRUE : FALSE) != FALSE;
}

inline bool ApplyHooks() {
    return g_loader != nullptr && g_loader->ApplyHooks() != FALSE;
}

inline bool RemoveHook(void* target) {
    return g_loader != nullptr && g_loader->RemoveHook(g_mod_id, target) != FALSE;
}

inline bool EnableHooks(void* const* targets, std::size_t count, bool enabled) {
    for (std::size_t index = 0; index < count; ++index) {
        if (targets[index] == nullptr || !QueueHook(targets[index], enabled)) return false;
    }
    return ApplyHooks();
}

inline std::uintptr_t Rva(std::uintptr_t rva) { return g_game_image.base + rva; }

inline bool HasVtable(std::uintptr_t object, std::uintptr_t vtable_rva) {
    return IsReadableRange(reinterpret_cast<const void*>(object), sizeof(std::uintptr_t)) &&
           *reinterpret_cast<const std::uintptr_t*>(object) == Rva(vtable_rva);
}

inline bool MatchesPrologue(std::uintptr_t address, const std::uint8_t* expected,
                            std::size_t length) {
    return IsReadableRange(reinterpret_cast<const void*>(address), length) &&
           std::memcmp(reinterpret_cast<const void*>(address), expected, length) == 0;
}

inline std::int64_t ScalePositive(std::int64_t value, std::int32_t multiplier,
                                  std::int32_t scale, std::int64_t maximum) {
    if (value <= 0 || multiplier <= scale || scale <= 0) return value;
    const std::int64_t quotient = value / scale;
    const std::int64_t remainder = value % scale;
    const std::int64_t extra = (remainder * multiplier + (scale / 2)) / scale;
    if (quotient > (maximum - extra) / multiplier) return maximum;
    return quotient * multiplier + extra;
}

}  // namespace dm
