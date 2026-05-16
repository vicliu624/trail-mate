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

struct TargetProfile
{
    const char* target_id = nullptr;
    const char* board_id = nullptr;
    const char* build_entrypoint = nullptr;
    const char* app_shell = nullptr;
    TargetPlatform platform = TargetPlatform::EspIdf;
    TargetRenderer renderer = TargetRenderer::Lvgl;
    const char* ux_pack_id = nullptr;
};

const TargetProfile* findTargetProfile(const char* target_id);
const TargetProfile* esp32LvglTargetProfiles(std::size_t* count);

} // namespace product_composition
