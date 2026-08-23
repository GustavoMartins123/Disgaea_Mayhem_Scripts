#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

// Stable C ABI shared by the loader, the Mod Menu and native mods.
// Every structure starts with its size so future ABI versions can reject
// incompatible callers explicitly instead of guessing a compatible layout.

constexpr std::uint32_t DM_MOD_LOADER_ABI_VERSION = 1;
constexpr std::size_t DM_MAX_MOD_OPTIONS = 16;

enum class DmModType : std::uint32_t {
    Toggle = 1,
    Action = 2,
    System = 3,
};

enum class DmModState : std::uint32_t {
    Discovered = 1,
    Loaded = 2,
    Enabled = 3,
    Disabled = 4,
    Failed = 5,
    ActionCompleted = 6,
};

enum class DmOptionType : std::uint32_t {
    Toggle = 1,
    SliderInt = 2,
    SliderFloat = 3,
};

struct DmModValue {
    std::uint32_t struct_size;
    DmOptionType type;
    BOOL bool_value;
    int int_value;
    float float_value;
};

struct DmModOptionView {
    std::uint32_t struct_size;
    char id[64];
    char name[96];
    DmOptionType type;
    DmModValue value;
    int min_int;
    int max_int;
    float min_float;
    float max_float;
};

struct DmModView {
    std::uint32_t struct_size;
    char id[64];
    char directory[64];
    char name[128];
    char category[64];
    char version[32];
    char author[64];
    char description[512];
    char action_label[32];
    DmModType type;
    DmModState state;
    BOOL configured_enabled;
    BOOL runtime_enabled;
    BOOL required;
    char status[192];
    std::uint32_t option_count;
    DmModOptionView options[DM_MAX_MOD_OPTIONS];
};

struct DmModLoaderApi {
    std::uint32_t struct_size;
    std::uint32_t abi_version;
    std::uint32_t(WINAPI* GetModCount)();
    BOOL(WINAPI* GetMod)(std::uint32_t index, DmModView* out_mod);
    BOOL(WINAPI* GetModById)(const char* mod_id, DmModView* out_mod);
    BOOL(WINAPI* SetModEnabled)(const char* mod_id, BOOL enabled);
    BOOL(WINAPI* SetModOption)(const char* mod_id, const char* option_id, const DmModValue* value);
    BOOL(WINAPI* FlushModConfig)(const char* mod_id);
    BOOL(WINAPI* ExecuteModAction)(const char* mod_id);
    void(WINAPI* Log)(const char* component, const char* message);
};

struct DmModHostContext {
    std::uint32_t struct_size;
    std::uint32_t abi_version;
    const DmModLoaderApi* loader;
    const char* game_directory;
    const char* mod_directory;
};

using DmModGetAbiVersionFn = std::uint32_t(WINAPI*)();
using DmModInitializeFn = BOOL(WINAPI*)(const DmModHostContext* context);
using DmModEnableFn = BOOL(WINAPI*)();
using DmModDisableFn = BOOL(WINAPI*)();
using DmModSetOptionFn = BOOL(WINAPI*)(const char* option_id, const DmModValue* value);
using DmModShutdownFn = void(WINAPI*)();
