#pragma once

#include <cstddef>

namespace product_composition
{

enum class TargetPlatform
{
    EspIdf,
    PlatformIo,
    Linux,
};

enum class TargetRenderer
{
    Lvgl,
    Gtk,
    Ascii,
    Headless,
};

enum class TargetSupportStatus
{
    Active,
    ActiveWithFallback,
    PendingHardwareValidation,
    Headless,
};

struct TargetProfile
{
    const char* target_id;
    const char* board_id;
    const char* build_entrypoint;
    const char* app_shell;
    const char* ux_pack_id;
    const char* ui_profile_id;
    const char* page_manifest_id;
    const char* layout_profile_id;
    TargetPlatform platform;
    TargetRenderer renderer;
    TargetSupportStatus status;
    bool has_display;
    bool has_touch;
    bool has_keyboard;
    bool has_trackball;
    bool has_lora;
    bool has_gps;
    bool has_audio;
};

const TargetProfile* findTargetProfile(const char* target_id);
const TargetProfile* allTargetProfiles(std::size_t* count);
const TargetProfile* esp32LvglTargetProfiles(std::size_t* count);

} // namespace product_composition
