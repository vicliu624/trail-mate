#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

#include "lvgl.h"
#include "ui/theme/theme_slots.h"

namespace ui::theme
{

extern const char kBuiltinAsciiThemeId[];
extern const char kBuiltinLegacyWarmThemeId[];
extern const char kOfficialLegacyWarmThemeId[];

struct ThemeInfo
{
    ThemeInfo() = default;
    ThemeInfo(const char* theme_id,
              const char* theme_display_name,
              const char* theme_base_id,
              bool theme_builtin)
        : id(theme_id), display_name(theme_display_name), base_theme_id(theme_base_id), builtin(theme_builtin)
    {
    }

    const char* id = nullptr;
    const char* display_name = nullptr;
    const char* base_theme_id = nullptr;
    bool builtin = false;
};

struct ResolvedComponentColor
{
    bool present = false;
    lv_color_t value = lv_color_hex(0x000000);
};

struct ComponentProfile
{
    ResolvedComponentColor bg_color{};
    ResolvedComponentColor border_color{};
    ResolvedComponentColor text_color{};
    ResolvedComponentColor accent_color{};
    ResolvedComponentColor shadow_color{};
    std::int16_t border_width = -1;
    std::int16_t radius = -1;
    std::int16_t pad_all = -1;
    std::int16_t pad_row = -1;
    std::int16_t pad_column = -1;
    std::int16_t pad_left = -1;
    std::int16_t pad_right = -1;
    std::int16_t pad_top = -1;
    std::int16_t pad_bottom = -1;
    std::int16_t min_height = -1;
    std::int16_t shadow_width = -1;
    std::int16_t shadow_opa = -1;
    std::int16_t bg_opa = -1;
};

void reset_active_theme();
bool set_active_theme(const char* theme_id);
const char* active_theme_id();
std::uint32_t active_theme_revision();
void reload_installed_themes();

std::size_t builtin_theme_count();
bool builtin_theme_at(std::size_t index, ThemeInfo& out_info);
std::size_t theme_count();
bool theme_at(std::size_t index, ThemeInfo& out_info);
bool describe_theme(const char* theme_id, ThemeInfo& out_info);
const char* preferred_default_theme_id();

lv_color_t color(ColorSlot slot);
lv_color_t color(const char* slot_id);

bool resolve_component_profile(ComponentSlot slot, ComponentProfile& out_profile);
bool resolve_component_profile(const char* slot_id, ComponentProfile& out_profile);

const lv_image_dsc_t* builtin_asset(AssetSlot slot);
const lv_image_dsc_t* resolve_builtin_asset(const char* slot_id);
bool resolve_asset_path(const char* slot_id, std::string& out_path);

} // namespace ui::theme
