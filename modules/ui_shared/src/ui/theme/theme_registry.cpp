#include "ui/theme/theme_registry.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ui/support/lvgl_fs_utils.h"

namespace ui::theme
{
const char kBuiltinAsciiThemeId[] = "builtin-ascii";
const char kBuiltinLegacyWarmThemeId[] = "builtin-legacy-warm";
const char kOfficialLegacyWarmThemeId[] = "official-legacy-warm";

namespace
{

constexpr std::size_t kColorSlotCount = static_cast<std::size_t>(ColorSlot::Count);
constexpr std::size_t kAssetSlotCount = static_cast<std::size_t>(AssetSlot::Count);
constexpr std::size_t kComponentSlotCount = static_cast<std::size_t>(ComponentSlot::Count);
constexpr std::size_t kInvalidIndex = static_cast<std::size_t>(-1);
constexpr std::size_t kMaxThemeFallbackDepth = 8;
#if defined(ARDUINO_ARCH_ESP32)
constexpr const char* kFlashThemePackRoot = "/fs/trailmate/packs/themes";
#endif
constexpr const char* kThemePackRoot = "/trailmate/packs/themes";

struct BuiltinThemeRecord
{
    constexpr BuiltinThemeRecord() = default;
    constexpr BuiltinThemeRecord(const ThemeInfo& theme_info,
                                 const std::uint32_t* theme_color_hex,
                                 const lv_image_dsc_t* const* theme_builtin_assets)
        : info(theme_info), color_hex(theme_color_hex), builtin_assets(theme_builtin_assets)
    {
    }

    ThemeInfo info;
    const std::uint32_t* color_hex = nullptr;
    const lv_image_dsc_t* const* builtin_assets = nullptr;
};

struct ManifestEntry
{
    std::string key;
    std::string value;
};

using Manifest = std::vector<ManifestEntry>;

enum class ComponentColorSourceKind : std::uint8_t
{
    None = 0,
    Literal,
    SlotRef,
};

struct ComponentColorSource
{
    ComponentColorSourceKind kind = ComponentColorSourceKind::None;
    std::uint32_t literal_hex = 0;
    ColorSlot slot = ColorSlot::PageBg;
};

struct ComponentProfileRecord
{
    ComponentColorSource bg_color{};
    ComponentColorSource border_color{};
    ComponentColorSource text_color{};
    ComponentColorSource accent_color{};
    ComponentColorSource shadow_color{};
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

struct ExternalThemeRecord
{
    std::string id;
    std::string display_name;
    std::string base_theme_id;
    std::array<bool, kColorSlotCount> has_color{};
    std::array<std::uint32_t, kColorSlotCount> color_hex{};
    std::array<std::string, kAssetSlotCount> asset_paths{};
    std::array<ComponentProfileRecord, kComponentSlotCount> component_profiles{};
    std::array<bool, kComponentSlotCount> has_component_profile{};
};

enum class ActiveThemeKind : std::uint8_t
{
    Builtin = 0,
    External,
};

constexpr std::uint32_t kAsciiColorHex[kColorSlotCount] = {
    0xF6F6F6, // color.bg.page
    0xFFFFFF, // color.bg.surface.primary
    0xE8E8E8, // color.bg.surface.secondary
    0x202020, // color.bg.chrome.topbar
    0x000000, // color.bg.overlay.scrim
    0x4A4A4A, // color.border.default
    0x1A1A1A, // color.border.strong
    0xB4B4B4, // color.separator.default
    0x111111, // color.text.primary
    0x5F5F5F, // color.text.muted
    0xFFFFFF, // color.text.inverse
    0x202020, // color.accent.primary
    0x000000, // color.accent.strong
    0x2E7D32, // color.state.ok
    0x1565C0, // color.state.info
    0xA15C00, // color.state.warn
    0xB00020, // color.state.error
    0xECECEC, // color.map.background
    0x202020, // color.map.route
    0xB00020, // color.map.track
    0xFFFFFF, // color.map.marker.border
    0x1F1F1F, // color.map.signal_label.bg
    0xA15C00, // color.map.node_marker
    0x1565C0, // color.map.self_marker
    0xA15C00, // color.map.link
    0xA15C00, // color.map.readout.id
    0x1565C0, // color.map.readout.lon
    0x2E7D32, // color.map.readout.lat
    0xA15C00, // color.map.readout.distance
    0xA15C00, // color.map.readout.protocol
    0x1565C0, // color.map.readout.rssi
    0x2E7D32, // color.map.readout.snr
    0x5F5F5F, // color.map.readout.seen
    0x202020, // color.map.readout.zoom
    0xFF3B30, // color.team.member.0
    0x34C759, // color.team.member.1
    0x007AFF, // color.team.member.2
    0xFFCC00, // color.team.member.3
    0xE3B11F, // color.gnss.system.gps
    0x2D6FB6, // color.gnss.system.gln
    0x3E7D3E, // color.gnss.system.gal
    0xB94A2C, // color.gnss.system.bd
    0x3E7D3E, // color.gnss.snr.good
    0x8FBF4D, // color.gnss.snr.fair
    0xA15C00, // color.gnss.snr.weak
    0xB00020, // color.gnss.snr.not_used
    0x6E6E6E, // color.gnss.snr.in_view
    0xA15C00, // color.sstv.meter.mid
};

constexpr std::uint32_t kLegacyWarmColorHex[kColorSlotCount] = {
    0xFFF3DF, // color.bg.page
    0xFFF7E9, // color.bg.surface.primary
    0xFFF0D3, // color.bg.surface.secondary
    0xEBA341, // color.bg.chrome.topbar
    0x000000, // color.bg.overlay.scrim
    0xD9B06A, // color.border.default
    0xB8792A, // color.border.strong
    0xE8D2AB, // color.separator.default
    0x3A2A1A, // color.text.primary
    0x6A5646, // color.text.muted
    0xFFFFFF, // color.text.inverse
    0xEBA341, // color.accent.primary
    0xD2871E, // color.accent.strong
    0x5BAF4A, // color.state.ok
    0x2F6FD6, // color.state.info
    0xC77700, // color.state.warn
    0xCC0000, // color.state.error
    0xF6E7C8, // color.map.background
    0xEBA341, // color.map.route
    0xFF2D55, // color.map.track
    0xFFFFFF, // color.map.marker.border
    0x1F1F1F, // color.map.signal_label.bg
    0xB94A2C, // color.map.node_marker
    0x2D6FB6, // color.map.self_marker
    0xC98118, // color.map.link
    0xC98118, // color.map.readout.id
    0x2D6FB6, // color.map.readout.lon
    0x3E7D3E, // color.map.readout.lat
    0xC98118, // color.map.readout.distance
    0xFFC75A, // color.map.readout.protocol
    0x68D5FF, // color.map.readout.rssi
    0x8BEA7B, // color.map.readout.snr
    0xFFF1D2, // color.map.readout.seen
    0xFFE6A9, // color.map.readout.zoom
    0xFF3B30, // color.team.member.0
    0x34C759, // color.team.member.1
    0x007AFF, // color.team.member.2
    0xFFCC00, // color.team.member.3
    0xE3B11F, // color.gnss.system.gps
    0x2D6FB6, // color.gnss.system.gln
    0x3E7D3E, // color.gnss.system.gal
    0xB94A2C, // color.gnss.system.bd
    0x3E7D3E, // color.gnss.snr.good
    0x8FBF4D, // color.gnss.snr.fair
    0xC18B2C, // color.gnss.snr.weak
    0xB94A2C, // color.gnss.snr.not_used
    0x6E6E6E, // color.gnss.snr.in_view
    0xC18B2C, // color.sstv.meter.mid
};

// The builtin theme surface no longer keeps compiled image descriptors. Official
// visuals now live in external theme packs, while firmware falls back to minimal
// text/symbol rendering when no external asset is installed.
const lv_image_dsc_t* const kMinimalBuiltinAssets[kAssetSlotCount] = {
    nullptr, // asset.boot.logo
    nullptr, // asset.notification.alert
    nullptr, // asset.status.route
    nullptr, // asset.status.tracker
    nullptr, // asset.status.gps
    nullptr, // asset.status.wifi
    nullptr, // asset.status.team
    nullptr, // asset.status.message
    nullptr, // asset.status.ble
    nullptr, // asset.menu.app.chat
    nullptr, // asset.menu.app.map
    nullptr, // asset.menu.app.sky_plot
    nullptr, // asset.menu.app.contacts
    nullptr, // asset.menu.app.energy_sweep
    nullptr, // asset.menu.app.team
    nullptr, // asset.menu.app.tracker
    nullptr, // asset.menu.app.pc_link
    nullptr, // asset.menu.app.sstv
    nullptr, // asset.menu.app.settings
    nullptr, // asset.menu.app.extensions
    nullptr, // asset.menu.app.usb
    nullptr, // asset.menu.app.walkie_talkie
    nullptr, // asset.map.self_marker
    nullptr, // asset.team_location.area_cleared
    nullptr, // asset.team_location.base_camp
    nullptr, // asset.team_location.good_find
    nullptr, // asset.team_location.rally
    nullptr, // asset.team_location.sos
};

const BuiltinThemeRecord kBuiltinThemes[] = {
    {ThemeInfo(kBuiltinAsciiThemeId, "Builtin ASCII", nullptr, true), kAsciiColorHex, kMinimalBuiltinAssets},
    {ThemeInfo(kBuiltinLegacyWarmThemeId, "Fallback Legacy Warm (Builtin)", kBuiltinAsciiThemeId, true),
     kLegacyWarmColorHex,
     kMinimalBuiltinAssets},
};

static_assert((sizeof(kBuiltinThemes) / sizeof(kBuiltinThemes[0])) == 2,
              "Builtin theme table changed; revisit default activation and compatibility fallback.");

std::vector<ExternalThemeRecord> s_external_themes;
bool s_external_themes_loaded = false;
ActiveThemeKind s_active_theme_kind = ActiveThemeKind::Builtin;
const BuiltinThemeRecord* s_active_builtin_theme = &kBuiltinThemes[1];
std::size_t s_active_external_theme_index = kInvalidIndex;
std::uint32_t s_theme_revision = 1;

void trim_in_place(std::string& value)
{
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0)
    {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
    {
        --end;
    }

    if (start == 0 && end == value.size())
    {
        return;
    }

    value = value.substr(start, end - start);
}

std::string trim_copy(std::string value)
{
    trim_in_place(value);
    return value;
}

std::string lowercase_ascii(std::string value)
{
    for (char& ch : value)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string normalize_dir_entry(const char* name)
{
    if (!name || !*name)
    {
        return {};
    }

    std::string normalized(name);
    while (!normalized.empty() &&
           (normalized.back() == '/' || normalized.back() == '\\'))
    {
        normalized.pop_back();
    }
    return normalized;
}

bool list_directory_entries(const char* path, std::vector<std::string>& out)
{
    out.clear();

    const std::string normalized = ::ui::fs::normalize_path(path);
    if (normalized.empty())
    {
        return false;
    }

    lv_fs_dir_t dir;
    if (lv_fs_dir_open(&dir, normalized.c_str()) != LV_FS_RES_OK)
    {
        return false;
    }

    char entry_name[256];
    while (true)
    {
        entry_name[0] = '\0';
        if (lv_fs_dir_read(&dir, entry_name, sizeof(entry_name)) != LV_FS_RES_OK)
        {
            break;
        }
        if (entry_name[0] == '\0')
        {
            break;
        }

        std::string entry = normalize_dir_entry(entry_name);
        if (entry.empty() || entry == "." || entry == "..")
        {
            continue;
        }
        out.push_back(std::move(entry));
    }

    lv_fs_dir_close(&dir);
    std::sort(out.begin(), out.end());
    return true;
}

std::string join_path(const std::string& base, const std::string& child)
{
    if (base.empty())
    {
        return child;
    }
    if (child.empty())
    {
        return base;
    }
    if (child.front() == '/')
    {
        return child;
    }
    if (base.back() == '/')
    {
        return base + child;
    }
    return base + "/" + child;
}

bool parse_manifest_file(const std::string& path, Manifest& out)
{
    out.clear();

    std::string contents;
    if (!::ui::fs::read_text_file(path.c_str(), contents))
    {
        return false;
    }

    std::size_t line_start = 0;
    while (line_start <= contents.size())
    {
        const std::size_t line_end = contents.find('\n', line_start);
        std::string line = contents.substr(
            line_start,
            line_end == std::string::npos ? std::string::npos : (line_end - line_start));
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        trim_in_place(line);

        if (!line.empty() && line[0] != '#' && line[0] != ';')
        {
            const std::size_t sep = line.find('=');
            if (sep != std::string::npos && sep > 0)
            {
                std::string key = lowercase_ascii(trim_copy(line.substr(0, sep)));
                std::string value = trim_copy(line.substr(sep + 1));
                if (!key.empty())
                {
                    out.push_back({std::move(key), std::move(value)});
                }
            }
        }

        if (line_end == std::string::npos)
        {
            break;
        }
        line_start = line_end + 1U;
    }

    return !out.empty();
}

const char* manifest_value(const Manifest& manifest, const char* key)
{
    if (!key)
    {
        return nullptr;
    }

    const std::string wanted = lowercase_ascii(key);
    for (const ManifestEntry& entry : manifest)
    {
        if (entry.key == wanted)
        {
            return entry.value.c_str();
        }
    }

    return nullptr;
}

std::string resolve_optional_file_path(const std::string& pack_dir,
                                       const Manifest& manifest,
                                       const char* manifest_key,
                                       const char* default_name)
{
    const char* configured = manifest_value(manifest, manifest_key);
    if (configured && configured[0] != '\0')
    {
        return join_path(pack_dir, configured);
    }

    const std::string fallback = join_path(pack_dir, default_name ? default_name : "");
    return ::ui::fs::file_exists(fallback.c_str()) ? fallback : std::string{};
}

bool parse_hex_color(std::string value, std::uint32_t& out_hex)
{
    trim_in_place(value);
    if (value.empty())
    {
        return false;
    }

    if (value.front() == '#')
    {
        value.erase(value.begin());
    }
    if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X'))
    {
        value = value.substr(2);
    }

    if (value.size() != 6 && value.size() != 8)
    {
        return false;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 16);
    if (end == value.c_str() || (end && *end != '\0'))
    {
        return false;
    }

    out_hex = static_cast<std::uint32_t>(parsed & 0xFFFFFFu);
    return true;
}

bool parse_int16_value(std::string value, std::int16_t& out_value)
{
    trim_in_place(value);
    if (value.empty())
    {
        return false;
    }

    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || (end && *end != '\0'))
    {
        return false;
    }

    if (parsed < -32768L || parsed > 32767L)
    {
        return false;
    }

    out_value = static_cast<std::int16_t>(parsed);
    return true;
}

bool parse_component_color_source(const std::string& value, ComponentColorSource& out_source)
{
    std::uint32_t hex = 0;
    if (parse_hex_color(value, hex))
    {
        out_source.kind = ComponentColorSourceKind::Literal;
        out_source.literal_hex = hex;
        return true;
    }

    ColorSlot slot = ColorSlot::PageBg;
    if (color_slot_from_id(value.c_str(), slot))
    {
        out_source.kind = ComponentColorSourceKind::SlotRef;
        out_source.slot = slot;
        out_source.literal_hex = 0;
        return true;
    }

    return false;
}

bool split_component_field_key(const char* raw_key,
                               ComponentSlot& out_slot,
                               std::string& out_field_name)
{
    if (!raw_key || !*raw_key)
    {
        return false;
    }

    const std::string key(raw_key);
    const std::size_t split_at = key.rfind('.');
    if (split_at == std::string::npos || split_at == 0 || split_at + 1 >= key.size())
    {
        return false;
    }

    const std::string prefix = key.substr(0, split_at);
    ComponentSlot slot = ComponentSlot::TopBarContainer;
    if (!component_slot_from_id(prefix.c_str(), slot))
    {
        return false;
    }

    out_slot = slot;
    out_field_name = key.substr(split_at + 1);
    return !out_field_name.empty();
}

bool assign_component_profile_field(ComponentProfileRecord& profile,
                                    const std::string& field_name,
                                    const std::string& field_value)
{
    if (field_name == "bg_color")
    {
        return parse_component_color_source(field_value, profile.bg_color);
    }
    if (field_name == "border_color")
    {
        return parse_component_color_source(field_value, profile.border_color);
    }
    if (field_name == "text_color")
    {
        return parse_component_color_source(field_value, profile.text_color);
    }
    if (field_name == "accent_color")
    {
        return parse_component_color_source(field_value, profile.accent_color);
    }
    if (field_name == "shadow_color")
    {
        return parse_component_color_source(field_value, profile.shadow_color);
    }
    if (field_name == "border_width")
    {
        return parse_int16_value(field_value, profile.border_width);
    }
    if (field_name == "radius")
    {
        return parse_int16_value(field_value, profile.radius);
    }
    if (field_name == "pad_all")
    {
        return parse_int16_value(field_value, profile.pad_all);
    }
    if (field_name == "pad_row")
    {
        return parse_int16_value(field_value, profile.pad_row);
    }
    if (field_name == "pad_column" || field_name == "gap")
    {
        return parse_int16_value(field_value, profile.pad_column);
    }
    if (field_name == "pad_left")
    {
        return parse_int16_value(field_value, profile.pad_left);
    }
    if (field_name == "pad_right")
    {
        return parse_int16_value(field_value, profile.pad_right);
    }
    if (field_name == "pad_top")
    {
        return parse_int16_value(field_value, profile.pad_top);
    }
    if (field_name == "pad_bottom")
    {
        return parse_int16_value(field_value, profile.pad_bottom);
    }
    if (field_name == "min_height")
    {
        return parse_int16_value(field_value, profile.min_height);
    }
    if (field_name == "shadow_width")
    {
        return parse_int16_value(field_value, profile.shadow_width);
    }
    if (field_name == "shadow_opa")
    {
        return parse_int16_value(field_value, profile.shadow_opa);
    }
    if (field_name == "bg_opa")
    {
        return parse_int16_value(field_value, profile.bg_opa);
    }

    return false;
}

const BuiltinThemeRecord* find_builtin_theme(const char* theme_id)
{
    if (!theme_id || !*theme_id)
    {
        return nullptr;
    }

    for (const BuiltinThemeRecord& theme : kBuiltinThemes)
    {
        if (std::strcmp(theme.info.id, theme_id) == 0)
        {
            return &theme;
        }
    }
    return nullptr;
}

std::size_t find_external_theme_index(const char* theme_id)
{
    if (!theme_id || !*theme_id)
    {
        return kInvalidIndex;
    }

    for (std::size_t index = 0; index < s_external_themes.size(); ++index)
    {
        if (s_external_themes[index].id == theme_id)
        {
            return index;
        }
    }
    return kInvalidIndex;
}

const ExternalThemeRecord* find_external_theme(const char* theme_id)
{
    const std::size_t index = find_external_theme_index(theme_id);
    return index == kInvalidIndex ? nullptr : &s_external_themes[index];
}

const ExternalThemeRecord* active_external_theme()
{
    return s_active_external_theme_index < s_external_themes.size()
               ? &s_external_themes[s_active_external_theme_index]
               : nullptr;
}

template <typename SlotEnum>
std::size_t slot_index(SlotEnum slot)
{
    return static_cast<std::size_t>(slot);
}

ThemeInfo make_external_theme_info(const ExternalThemeRecord& record)
{
    return ThemeInfo(record.id.c_str(),
                     record.display_name.c_str(),
                     record.base_theme_id.empty() ? nullptr : record.base_theme_id.c_str(),
                     false);
}

bool parse_theme_tokens(const std::string& path, ExternalThemeRecord& record)
{
    Manifest tokens;
    if (!parse_manifest_file(path, tokens))
    {
        return false;
    }

    for (const ManifestEntry& entry : tokens)
    {
        ColorSlot slot = ColorSlot::PageBg;
        if (!color_slot_from_id(entry.key.c_str(), slot))
        {
            return false;
        }

        std::uint32_t hex = 0;
        if (!parse_hex_color(entry.value, hex))
        {
            return false;
        }

        const std::size_t index = slot_index(slot);
        record.has_color[index] = true;
        record.color_hex[index] = hex;
    }

    return true;
}

bool parse_theme_assets(const std::string& path,
                        const std::string& pack_dir,
                        ExternalThemeRecord& record)
{
    Manifest assets;
    if (!parse_manifest_file(path, assets))
    {
        return false;
    }

    for (const ManifestEntry& entry : assets)
    {
        AssetSlot slot = AssetSlot::BootLogo;
        if (!asset_slot_from_id(entry.key.c_str(), slot))
        {
            return false;
        }

        std::string resolved = join_path(pack_dir, entry.value);
        std::string normalized = ::ui::fs::normalize_path(resolved.c_str());
        const char* candidate_path = !normalized.empty() ? normalized.c_str() : resolved.c_str();
        if (!::ui::fs::file_exists(candidate_path))
        {
            return false;
        }
        if (!normalized.empty())
        {
            record.asset_paths[slot_index(slot)] = std::move(normalized);
        }
        else
        {
            record.asset_paths[slot_index(slot)] = std::move(resolved);
        }
    }

    return true;
}

bool parse_theme_components(const std::string& path, ExternalThemeRecord& record)
{
    Manifest components;
    if (!parse_manifest_file(path, components))
    {
        return false;
    }

    for (const ManifestEntry& entry : components)
    {
        ComponentSlot slot = ComponentSlot::TopBarContainer;
        std::string field_name;
        if (!split_component_field_key(entry.key.c_str(), slot, field_name))
        {
            return false;
        }

        const std::size_t index = slot_index(slot);
        if (!assign_component_profile_field(record.component_profiles[index], field_name, entry.value))
        {
            return false;
        }
        record.has_component_profile[index] = true;
    }

    return true;
}

void load_external_themes_from_root(const char* root_path)
{
    std::vector<std::string> entries;
    if (!list_directory_entries(root_path, entries))
    {
        return;
    }

    for (const std::string& entry : entries)
    {
        const std::string pack_dir = join_path(root_path, entry);
        const std::string manifest_path = join_path(pack_dir, "manifest.ini");

        Manifest manifest;
        if (!parse_manifest_file(manifest_path, manifest))
        {
            continue;
        }

        const char* id_value = manifest_value(manifest, "id");
        const std::string id = (id_value && id_value[0] != '\0') ? id_value : entry;
        if (id.empty() || find_builtin_theme(id.c_str()) || find_external_theme(id.c_str()))
        {
            continue;
        }

        ExternalThemeRecord record{};
        record.id = id;
        const char* display_name = manifest_value(manifest, "display_name");
        if (display_name && display_name[0] != '\0')
        {
            record.display_name = display_name;
        }
        else
        {
            record.display_name = record.id;
        }
        const char* base_theme = manifest_value(manifest, "base_theme");
        if (base_theme && base_theme[0] != '\0')
        {
            record.base_theme_id = base_theme;
        }

        bool valid = true;

        const std::string tokens_path =
            resolve_optional_file_path(pack_dir, manifest, "tokens", "tokens.ini");
        if (!tokens_path.empty())
        {
            valid = parse_theme_tokens(tokens_path, record) && valid;
        }

        const std::string components_path =
            resolve_optional_file_path(pack_dir, manifest, "components", "components.ini");
        if (!components_path.empty())
        {
            valid = parse_theme_components(components_path, record) && valid;
        }

        const std::string assets_path =
            resolve_optional_file_path(pack_dir, manifest, "assets", "assets.ini");
        if (!assets_path.empty())
        {
            valid = parse_theme_assets(assets_path, pack_dir, record) && valid;
        }

        if (!valid)
        {
            continue;
        }

        s_external_themes.push_back(std::move(record));
    }
}

void ensure_external_themes_loaded()
{
    if (s_external_themes_loaded)
    {
        return;
    }

    s_external_themes_loaded = true;
    s_external_themes.clear();

#if defined(ARDUINO_ARCH_ESP32)
    load_external_themes_from_root(kFlashThemePackRoot);
#endif
    load_external_themes_from_root(kThemePackRoot);

    std::sort(s_external_themes.begin(),
              s_external_themes.end(),
              [](const ExternalThemeRecord& lhs, const ExternalThemeRecord& rhs)
              {
                  return lowercase_ascii(lhs.display_name) < lowercase_ascii(rhs.display_name);
              });
}

bool resolve_color_hex_for_theme_id(const char* theme_id,
                                    std::size_t color_index,
                                    std::uint32_t& out_hex,
                                    std::size_t depth)
{
    if (depth > kMaxThemeFallbackDepth || color_index >= kColorSlotCount)
    {
        return false;
    }

    if (const ExternalThemeRecord* theme = find_external_theme(theme_id))
    {
        if (theme->has_color[color_index])
        {
            out_hex = theme->color_hex[color_index];
            return true;
        }
        if (!theme->base_theme_id.empty())
        {
            return resolve_color_hex_for_theme_id(
                theme->base_theme_id.c_str(), color_index, out_hex, depth + 1U);
        }
    }

    if (const BuiltinThemeRecord* builtin = find_builtin_theme(theme_id))
    {
        out_hex = builtin->color_hex[color_index];
        return true;
    }

    return false;
}

const lv_image_dsc_t* resolve_builtin_asset_for_theme_id(const char* theme_id,
                                                         std::size_t asset_index,
                                                         std::size_t depth)
{
    if (depth > kMaxThemeFallbackDepth || asset_index >= kAssetSlotCount)
    {
        return nullptr;
    }

    if (const ExternalThemeRecord* theme = find_external_theme(theme_id))
    {
        if (!theme->base_theme_id.empty())
        {
            return resolve_builtin_asset_for_theme_id(
                theme->base_theme_id.c_str(), asset_index, depth + 1U);
        }
        return nullptr;
    }

    if (const BuiltinThemeRecord* builtin = find_builtin_theme(theme_id))
    {
        return builtin->builtin_assets[asset_index];
    }

    return nullptr;
}

bool resolve_asset_path_for_theme_id(const char* theme_id,
                                     std::size_t asset_index,
                                     std::string& out_path,
                                     std::size_t depth)
{
    if (depth > kMaxThemeFallbackDepth || asset_index >= kAssetSlotCount)
    {
        out_path.clear();
        return false;
    }

    if (const ExternalThemeRecord* theme = find_external_theme(theme_id))
    {
        if (!theme->asset_paths[asset_index].empty())
        {
            out_path = theme->asset_paths[asset_index];
            return true;
        }
        if (!theme->base_theme_id.empty())
        {
            return resolve_asset_path_for_theme_id(
                theme->base_theme_id.c_str(), asset_index, out_path, depth + 1U);
        }
    }

    out_path.clear();
    return false;
}

bool resolve_component_color_for_theme_id(const char* root_theme_id,
                                          const ComponentColorSource& source,
                                          ResolvedComponentColor& out_color)
{
    if (source.kind == ComponentColorSourceKind::None)
    {
        return false;
    }

    if (source.kind == ComponentColorSourceKind::Literal)
    {
        out_color.present = true;
        out_color.value = lv_color_hex(source.literal_hex);
        return true;
    }

    std::uint32_t hex = 0;
    if (!resolve_color_hex_for_theme_id(root_theme_id, slot_index(source.slot), hex, 0))
    {
        return false;
    }

    out_color.present = true;
    out_color.value = lv_color_hex(hex);
    return true;
}

void apply_component_profile_record(const char* root_theme_id,
                                    const ComponentProfileRecord& record,
                                    ComponentProfile& out_profile)
{
    (void)resolve_component_color_for_theme_id(root_theme_id, record.bg_color, out_profile.bg_color);
    (void)resolve_component_color_for_theme_id(root_theme_id, record.border_color, out_profile.border_color);
    (void)resolve_component_color_for_theme_id(root_theme_id, record.text_color, out_profile.text_color);
    (void)resolve_component_color_for_theme_id(root_theme_id, record.accent_color, out_profile.accent_color);
    (void)resolve_component_color_for_theme_id(root_theme_id, record.shadow_color, out_profile.shadow_color);

    if (record.border_width >= 0)
    {
        out_profile.border_width = record.border_width;
    }
    if (record.radius >= 0)
    {
        out_profile.radius = record.radius;
    }
    if (record.pad_all >= 0)
    {
        out_profile.pad_all = record.pad_all;
    }
    if (record.pad_row >= 0)
    {
        out_profile.pad_row = record.pad_row;
    }
    if (record.pad_column >= 0)
    {
        out_profile.pad_column = record.pad_column;
    }
    if (record.pad_left >= 0)
    {
        out_profile.pad_left = record.pad_left;
    }
    if (record.pad_right >= 0)
    {
        out_profile.pad_right = record.pad_right;
    }
    if (record.pad_top >= 0)
    {
        out_profile.pad_top = record.pad_top;
    }
    if (record.pad_bottom >= 0)
    {
        out_profile.pad_bottom = record.pad_bottom;
    }
    if (record.min_height >= 0)
    {
        out_profile.min_height = record.min_height;
    }
    if (record.shadow_width >= 0)
    {
        out_profile.shadow_width = record.shadow_width;
    }
    if (record.shadow_opa >= 0)
    {
        out_profile.shadow_opa = record.shadow_opa;
    }
    if (record.bg_opa >= 0)
    {
        out_profile.bg_opa = record.bg_opa;
    }
}

bool component_profile_has_values(const ComponentProfile& profile)
{
    return profile.bg_color.present || profile.border_color.present || profile.text_color.present ||
           profile.accent_color.present || profile.shadow_color.present ||
           profile.border_width >= 0 || profile.radius >= 0 || profile.pad_all >= 0 ||
           profile.pad_row >= 0 || profile.pad_column >= 0 || profile.pad_left >= 0 ||
           profile.pad_right >= 0 || profile.pad_top >= 0 || profile.pad_bottom >= 0 ||
           profile.min_height >= 0 || profile.shadow_width >= 0 ||
           profile.shadow_opa >= 0 || profile.bg_opa >= 0;
}

bool resolve_component_profile_for_theme_id(const char* root_theme_id,
                                            const char* theme_id,
                                            std::size_t component_index,
                                            ComponentProfile& out_profile,
                                            std::size_t depth)
{
    if (depth > kMaxThemeFallbackDepth || component_index >= kComponentSlotCount)
    {
        return false;
    }

    bool resolved_any = false;

    if (const ExternalThemeRecord* theme = find_external_theme(theme_id))
    {
        if (!theme->base_theme_id.empty())
        {
            resolved_any = resolve_component_profile_for_theme_id(root_theme_id,
                                                                 theme->base_theme_id.c_str(),
                                                                 component_index,
                                                                 out_profile,
                                                                 depth + 1U) || resolved_any;
        }

        if (theme->has_component_profile[component_index])
        {
            apply_component_profile_record(root_theme_id,
                                           theme->component_profiles[component_index],
                                           out_profile);
            resolved_any = true;
        }
    }

    return resolved_any || component_profile_has_values(out_profile);
}

const char* current_active_theme_id()
{
    if (s_active_theme_kind == ActiveThemeKind::External)
    {
        if (const ExternalThemeRecord* theme = active_external_theme())
        {
            return theme->id.c_str();
        }
    }
    return s_active_builtin_theme ? s_active_builtin_theme->info.id : kBuiltinLegacyWarmThemeId;
}

const char* canonical_theme_id_impl(const char* theme_id)
{
    if (!theme_id || !*theme_id)
    {
        return nullptr;
    }

    if (std::strcmp(theme_id, kBuiltinLegacyWarmThemeId) == 0 &&
        find_external_theme(kOfficialLegacyWarmThemeId) != nullptr)
    {
        return kOfficialLegacyWarmThemeId;
    }

    return theme_id;
}

bool should_expose_theme_publicly(const ThemeInfo& info)
{
    if (!info.id)
    {
        return false;
    }

    if (std::strcmp(info.id, kBuiltinLegacyWarmThemeId) == 0 &&
        find_external_theme(kOfficialLegacyWarmThemeId) != nullptr)
    {
        return false;
    }

    return true;
}

int public_theme_rank(const ThemeInfo& info)
{
    if (info.id && std::strcmp(info.id, kOfficialLegacyWarmThemeId) == 0)
    {
        return 0;
    }

    if (!info.builtin)
    {
        return 1;
    }

    if (info.id && std::strcmp(info.id, kBuiltinAsciiThemeId) == 0)
    {
        return 2;
    }

    return 3;
}

void collect_public_theme_infos(std::vector<ThemeInfo>& out_infos)
{
    out_infos.clear();
    out_infos.reserve(builtin_theme_count() + s_external_themes.size());

    for (const ExternalThemeRecord& record : s_external_themes)
    {
        ThemeInfo info = make_external_theme_info(record);
        if (should_expose_theme_publicly(info))
        {
            out_infos.push_back(info);
        }
    }

    for (std::size_t index = 0; index < builtin_theme_count(); ++index)
    {
        ThemeInfo info{};
        if (builtin_theme_at(index, info) && should_expose_theme_publicly(info))
        {
            out_infos.push_back(info);
        }
    }

    std::sort(out_infos.begin(),
              out_infos.end(),
              [](const ThemeInfo& lhs, const ThemeInfo& rhs)
              {
                  const int lhs_rank = public_theme_rank(lhs);
                  const int rhs_rank = public_theme_rank(rhs);
                  if (lhs_rank != rhs_rank)
                  {
                      return lhs_rank < rhs_rank;
                  }

                  const char* lhs_name = lhs.display_name ? lhs.display_name : "";
                  const char* rhs_name = rhs.display_name ? rhs.display_name : "";
                  return lowercase_ascii(lhs_name) < lowercase_ascii(rhs_name);
              });
}

} // namespace

void reset_active_theme()
{
    ensure_external_themes_loaded();
    (void)set_active_theme(preferred_default_theme_id());
}

bool set_active_theme(const char* theme_id)
{
    ensure_external_themes_loaded();
    theme_id = canonical_theme_id_impl(theme_id);

    if (const BuiltinThemeRecord* theme = find_builtin_theme(theme_id))
    {
        if (s_active_theme_kind != ActiveThemeKind::Builtin || s_active_builtin_theme != theme)
        {
            s_active_theme_kind = ActiveThemeKind::Builtin;
            s_active_builtin_theme = theme;
            s_active_external_theme_index = kInvalidIndex;
            ++s_theme_revision;
        }
        return true;
    }

    const std::size_t external_index = find_external_theme_index(theme_id);
    if (external_index != kInvalidIndex)
    {
        if (s_active_theme_kind != ActiveThemeKind::External ||
            s_active_external_theme_index != external_index)
        {
            s_active_theme_kind = ActiveThemeKind::External;
            s_active_external_theme_index = external_index;
            s_active_builtin_theme = nullptr;
            ++s_theme_revision;
        }
        return true;
    }

    return false;
}

const char* active_theme_id()
{
    ensure_external_themes_loaded();
    return current_active_theme_id();
}

std::uint32_t active_theme_revision()
{
    return s_theme_revision;
}

void reload_installed_themes()
{
    const std::string previous_active_id = active_theme_id();

    s_external_themes.clear();
    s_external_themes_loaded = false;
    ensure_external_themes_loaded();

    if (!previous_active_id.empty() && set_active_theme(previous_active_id.c_str()))
    {
        return;
    }

    reset_active_theme();
}

std::size_t builtin_theme_count()
{
    return sizeof(kBuiltinThemes) / sizeof(kBuiltinThemes[0]);
}

bool builtin_theme_at(std::size_t index, ThemeInfo& out_info)
{
    if (index >= builtin_theme_count())
    {
        out_info = ThemeInfo{};
        return false;
    }

    out_info = kBuiltinThemes[index].info;
    return true;
}

std::size_t theme_count()
{
    ensure_external_themes_loaded();
    std::vector<ThemeInfo> themes;
    collect_public_theme_infos(themes);
    return themes.size();
}

bool theme_at(std::size_t index, ThemeInfo& out_info)
{
    ensure_external_themes_loaded();
    std::vector<ThemeInfo> themes;
    collect_public_theme_infos(themes);
    if (index >= themes.size())
    {
        out_info = ThemeInfo{};
        return false;
    }

    out_info = themes[index];
    return true;
}

bool describe_theme(const char* theme_id, ThemeInfo& out_info)
{
    ensure_external_themes_loaded();
    theme_id = canonical_theme_id_impl(theme_id);

    if (const BuiltinThemeRecord* theme = find_builtin_theme(theme_id))
    {
        out_info = theme->info;
        return true;
    }

    if (const ExternalThemeRecord* theme = find_external_theme(theme_id))
    {
        out_info = make_external_theme_info(*theme);
        return true;
    }

    out_info = ThemeInfo{};
    return false;
}

const char* preferred_default_theme_id()
{
    ensure_external_themes_loaded();
    return find_external_theme(kOfficialLegacyWarmThemeId) != nullptr
               ? kOfficialLegacyWarmThemeId
               : kBuiltinLegacyWarmThemeId;
}

lv_color_t color(ColorSlot slot)
{
    ensure_external_themes_loaded();

    const std::size_t index = slot_index(slot);
    if (index >= kColorSlotCount)
    {
        return lv_color_hex(0x000000);
    }

    std::uint32_t hex = 0x000000;
    if (resolve_color_hex_for_theme_id(active_theme_id(), index, hex, 0))
    {
        return lv_color_hex(hex);
    }

    return lv_color_hex(kLegacyWarmColorHex[index]);
}

lv_color_t color(const char* slot_id)
{
    ColorSlot slot = ColorSlot::PageBg;
    if (!color_slot_from_id(slot_id, slot))
    {
        return lv_color_hex(0x000000);
    }
    return color(slot);
}

bool resolve_component_profile(ComponentSlot slot, ComponentProfile& out_profile)
{
    ensure_external_themes_loaded();

    out_profile = ComponentProfile{};
    const std::size_t index = slot_index(slot);
    if (index >= kComponentSlotCount)
    {
        return false;
    }

    return resolve_component_profile_for_theme_id(active_theme_id(),
                                                 active_theme_id(),
                                                 index,
                                                 out_profile,
                                                 0);
}

bool resolve_component_profile(const char* slot_id, ComponentProfile& out_profile)
{
    ComponentSlot slot = ComponentSlot::TopBarContainer;
    if (!component_slot_from_id(slot_id, slot))
    {
        out_profile = ComponentProfile{};
        return false;
    }

    return resolve_component_profile(slot, out_profile);
}

const lv_image_dsc_t* builtin_asset(AssetSlot slot)
{
    ensure_external_themes_loaded();

    const std::size_t index = slot_index(slot);
    if (index >= kAssetSlotCount)
    {
        return nullptr;
    }

    return resolve_builtin_asset_for_theme_id(active_theme_id(), index, 0);
}

const lv_image_dsc_t* resolve_builtin_asset(const char* slot_id)
{
    AssetSlot slot = AssetSlot::BootLogo;
    if (!asset_slot_from_id(slot_id, slot))
    {
        return nullptr;
    }
    return builtin_asset(slot);
}

bool resolve_asset_path(const char* slot_id, std::string& out_path)
{
    ensure_external_themes_loaded();

    AssetSlot slot = AssetSlot::BootLogo;
    if (!asset_slot_from_id(slot_id, slot))
    {
        out_path.clear();
        return false;
    }

    return resolve_asset_path_for_theme_id(active_theme_id(), slot_index(slot), out_path, 0);
}

} // namespace ui::theme
