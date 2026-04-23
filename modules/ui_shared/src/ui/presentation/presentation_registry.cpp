#include "ui/presentation/presentation_registry.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ui/presentation/presentation_contract.h"
#include "ui/support/lvgl_fs_utils.h"

namespace ui::presentation
{

const char kBuiltinDefaultPresentationId[] = "builtin-default";
const char kBuiltinDirectoryStackedPresentationId[] = "builtin-directory-stacked";
const char kOfficialDefaultPresentationId[] = "official-default";
const char kOfficialDirectoryStackedPresentationId[] = "official-directory-stacked";

namespace
{

constexpr std::size_t kInvalidIndex = static_cast<std::size_t>(-1);
#if defined(ARDUINO_ARCH_ESP32)
constexpr const char* kFlashPresentationPackRoot = "/fs/trailmate/packs/presentations";
#endif
constexpr const char* kPresentationPackRoot = "/trailmate/packs/presentations";

struct PresentationLayoutRecord
{
    constexpr PresentationLayoutRecord() = default;
    constexpr PresentationLayoutRecord(MenuDashboardLayout menu_dashboard_layout,
                                       BootSplashLayout boot_splash_layout,
                                       WatchFaceLayout watch_face_layout,
                                       ChatListLayout chat_list_layout,
                                       DirectoryBrowserLayout directory_browser_layout,
                                       ChatConversationLayout chat_conversation_layout,
                                       ChatComposeLayout chat_compose_layout,
                                       MapFocusLayout map_focus_layout,
                                       InstrumentPanelLayout instrument_panel_layout,
                                       ServicePanelLayout service_panel_layout)
        : menu_dashboard(menu_dashboard_layout),
          boot_splash(boot_splash_layout),
          watch_face(watch_face_layout),
          chat_list(chat_list_layout),
          directory_browser(directory_browser_layout),
          chat_conversation(chat_conversation_layout),
          chat_compose(chat_compose_layout),
          map_focus(map_focus_layout),
          instrument_panel(instrument_panel_layout),
          service_panel(service_panel_layout)
    {
    }

    MenuDashboardLayout menu_dashboard = MenuDashboardLayout::LauncherGrid;
    BootSplashLayout boot_splash = BootSplashLayout::CenteredHeroLog;
    WatchFaceLayout watch_face = WatchFaceLayout::StandbyClock;
    ChatListLayout chat_list = ChatListLayout::ConversationDirectory;
    DirectoryBrowserLayout directory_browser = DirectoryBrowserLayout::SplitSidebar;
    ChatConversationLayout chat_conversation = ChatConversationLayout::Standard;
    ChatComposeLayout chat_compose = ChatComposeLayout::Standard;
    MapFocusLayout map_focus = MapFocusLayout::OverlayPanels;
    InstrumentPanelLayout instrument_panel = InstrumentPanelLayout::SplitCanvasLegend;
    ServicePanelLayout service_panel = ServicePanelLayout::PrimaryPanelWithFooter;
};

struct PresentationLayoutPresence
{
    bool menu_dashboard = false;
    bool boot_splash = false;
    bool watch_face = false;
    bool chat_list = false;
    bool directory_browser = false;
    bool chat_conversation = false;
    bool chat_compose = false;
    bool map_focus = false;
    bool instrument_panel = false;
    bool service_panel = false;
};

struct BuiltinPresentationRecord
{
    constexpr BuiltinPresentationRecord() = default;
    constexpr BuiltinPresentationRecord(const PresentationInfo& presentation_info,
                                        const PresentationLayoutRecord& presentation_layouts)
        : info(presentation_info), layouts(presentation_layouts)
    {
    }

    PresentationInfo info;
    PresentationLayoutRecord layouts{};
};

struct ManifestEntry
{
    std::string key;
    std::string value;
};

using Manifest = std::vector<ManifestEntry>;

struct ExternalPresentationRecord
{
    std::string id;
    std::string display_name;
    std::string base_presentation_id;
    PresentationLayoutRecord layouts{};
    PresentationLayoutPresence has_layouts{};
    MenuDashboardSchema menu_dashboard_schema{};
    bool has_menu_dashboard_schema = false;
    BootSplashSchema boot_splash_schema{};
    bool has_boot_splash_schema = false;
    WatchFaceSchema watch_face_schema{};
    bool has_watch_face_schema = false;
    DirectoryBrowserSchema directory_browser_schema{};
    bool has_directory_browser_schema = false;
    ChatConversationSchema chat_conversation_schema{};
    bool has_chat_conversation_schema = false;
    ChatComposeSchema chat_compose_schema{};
    bool has_chat_compose_schema = false;
    MapFocusSchema map_focus_schema{};
    bool has_map_focus_schema = false;
    InstrumentPanelSchema instrument_panel_schema{};
    bool has_instrument_panel_schema = false;
    ServicePanelSchema service_panel_schema{};
    bool has_service_panel_schema = false;
};

enum class ActivePresentationKind : std::uint8_t
{
    Builtin = 0,
    External,
};

const BuiltinPresentationRecord kBuiltinPresentations[] = {
    {PresentationInfo(kBuiltinDefaultPresentationId,
                      "Fallback Directory Split Sidebar (Builtin)",
                      nullptr,
                      true),
     PresentationLayoutRecord(MenuDashboardLayout::SimpleList,
                              BootSplashLayout::CenteredHeroLog,
                              WatchFaceLayout::StandbyClock,
                              ChatListLayout::ConversationDirectory,
                              DirectoryBrowserLayout::SplitSidebar,
                              ChatConversationLayout::Standard,
                              ChatComposeLayout::Standard,
                              MapFocusLayout::OverlayPanels,
                              InstrumentPanelLayout::SplitCanvasLegend,
                              ServicePanelLayout::PrimaryPanelWithFooter)},
    {PresentationInfo(kBuiltinDirectoryStackedPresentationId,
                      "Fallback Directory Stacked Selectors (Builtin)",
                      kBuiltinDefaultPresentationId,
                      true),
     PresentationLayoutRecord(MenuDashboardLayout::SimpleList,
                              BootSplashLayout::CenteredHeroLog,
                              WatchFaceLayout::StandbyClock,
                              ChatListLayout::ConversationDirectory,
                              DirectoryBrowserLayout::StackedSelectors,
                              ChatConversationLayout::Standard,
                              ChatComposeLayout::Standard,
                              MapFocusLayout::OverlayPanels,
                              InstrumentPanelLayout::SplitCanvasLegend,
                              ServicePanelLayout::PrimaryPanelWithFooter)},
};

std::vector<ExternalPresentationRecord> s_external_presentations;
bool s_external_presentations_loaded = false;
ActivePresentationKind s_active_presentation_kind = ActivePresentationKind::Builtin;
const BuiltinPresentationRecord* s_active_builtin_presentation = &kBuiltinPresentations[0];
std::size_t s_active_external_presentation_index = kInvalidIndex;
std::uint32_t s_presentation_revision = 1;

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

bool parse_int16_value(const std::string& value, std::int16_t& out_value)
{
    const std::string trimmed = trim_copy(value);
    if (trimmed.empty())
    {
        return false;
    }

    char* end = nullptr;
    const long parsed = std::strtol(trimmed.c_str(), &end, 10);
    if (!end || *end != '\0' ||
        parsed < static_cast<long>(INT16_MIN) ||
        parsed > static_cast<long>(INT16_MAX))
    {
        return false;
    }

    out_value = static_cast<std::int16_t>(parsed);
    return true;
}

bool parse_percent_value(const std::string& value, std::int16_t& out_value)
{
    std::int16_t parsed = out_value;
    if (!parse_int16_value(value, parsed) || parsed <= 0 || parsed > 100)
    {
        return false;
    }

    out_value = parsed;
    return true;
}

bool parse_bool_value(const std::string& value, bool& out_value)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    if (normalized == "true" || normalized == "yes" || normalized == "1" ||
        normalized == "on")
    {
        out_value = true;
        return true;
    }
    if (normalized == "false" || normalized == "no" || normalized == "0" ||
        normalized == "off")
    {
        out_value = false;
        return true;
    }
    return false;
}

bool parse_schema_align(const std::string& value, SchemaAlign& out_align)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    if (normalized == "start" || normalized == "left" || normalized == "top")
    {
        out_align = SchemaAlign::Start;
        return true;
    }
    if (normalized == "center" || normalized == "middle")
    {
        out_align = SchemaAlign::Center;
        return true;
    }
    if (normalized == "end" || normalized == "right" || normalized == "bottom")
    {
        out_align = SchemaAlign::End;
        return true;
    }
    return false;
}

bool parse_schema_corner(const std::string& value, SchemaCorner& out_corner)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    if (normalized == "top-right" || normalized == "top_right" || normalized == "right-top" ||
        normalized == "right_top")
    {
        out_corner = SchemaCorner::TopRight;
        return true;
    }
    if (normalized == "top-left" || normalized == "top_left" || normalized == "left-top" ||
        normalized == "left_top")
    {
        out_corner = SchemaCorner::TopLeft;
        return true;
    }
    if (normalized == "bottom-right" || normalized == "bottom_right" ||
        normalized == "right-bottom" || normalized == "right_bottom")
    {
        out_corner = SchemaCorner::BottomRight;
        return true;
    }
    if (normalized == "bottom-left" || normalized == "bottom_left" || normalized == "left-bottom" ||
        normalized == "left_bottom")
    {
        out_corner = SchemaCorner::BottomLeft;
        return true;
    }
    return false;
}

bool parse_schema_flow(const std::string& value, SchemaFlow& out_flow)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    if (normalized == "column" || normalized == "vertical")
    {
        out_flow = SchemaFlow::Column;
        return true;
    }
    if (normalized == "row" || normalized == "horizontal")
    {
        out_flow = SchemaFlow::Row;
        return true;
    }
    return false;
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

const BuiltinPresentationRecord* find_builtin_presentation(const char* presentation_id)
{
    if (!presentation_id || !*presentation_id)
    {
        return nullptr;
    }

    for (const BuiltinPresentationRecord& presentation : kBuiltinPresentations)
    {
        if (std::strcmp(presentation.info.id, presentation_id) == 0)
        {
            return &presentation;
        }
    }

    return nullptr;
}

std::size_t find_external_presentation_index(const char* presentation_id)
{
    if (!presentation_id || !*presentation_id)
    {
        return kInvalidIndex;
    }

    for (std::size_t index = 0; index < s_external_presentations.size(); ++index)
    {
        if (s_external_presentations[index].id == presentation_id)
        {
            return index;
        }
    }

    return kInvalidIndex;
}

const ExternalPresentationRecord* find_external_presentation(const char* presentation_id)
{
    const std::size_t index = find_external_presentation_index(presentation_id);
    return index == kInvalidIndex ? nullptr : &s_external_presentations[index];
}

const ExternalPresentationRecord* active_external_presentation()
{
    return s_active_external_presentation_index < s_external_presentations.size()
               ? &s_external_presentations[s_active_external_presentation_index]
               : nullptr;
}

PresentationInfo make_external_presentation_info(const ExternalPresentationRecord& record)
{
    return PresentationInfo(record.id.c_str(),
                            record.display_name.c_str(),
                            record.base_presentation_id.empty()
                                ? nullptr
                                : record.base_presentation_id.c_str(),
                            false);
}

bool parse_menu_dashboard_variant(const std::string& value, MenuDashboardLayout& out_layout)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    if (normalized == "simple-list" || normalized == "simple_list" ||
        normalized == "list" || normalized == "builtin-default")
    {
        out_layout = MenuDashboardLayout::SimpleList;
        return true;
    }
    if (normalized == "launcher-grid" || normalized == "launcher_grid" ||
        normalized == "default" || normalized == "standard" ||
        normalized == "official-default")
    {
        out_layout = MenuDashboardLayout::LauncherGrid;
        return true;
    }
    return false;
}

bool parse_boot_splash_variant(const std::string& value, BootSplashLayout& out_layout)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    if (normalized == "centered-hero-log" || normalized == "centered_hero_log" ||
        normalized == "hero-log" || normalized == "hero_log" ||
        normalized == "default" || normalized == "standard" ||
        normalized == "official-default")
    {
        out_layout = BootSplashLayout::CenteredHeroLog;
        return true;
    }
    return false;
}

bool parse_watch_face_variant(const std::string& value, WatchFaceLayout& out_layout)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    if (normalized == "standby-clock" || normalized == "standby_clock" ||
        normalized == "standby" || normalized == "default" ||
        normalized == "standard" || normalized == "official-default")
    {
        out_layout = WatchFaceLayout::StandbyClock;
        return true;
    }
    return false;
}

bool parse_schema_version(const std::string& value);
bool key_matches(const std::string& key,
                 const char* canonical,
                 const char* alias1 = nullptr,
                 const char* alias2 = nullptr);

bool apply_menu_dashboard_schema_field(const ManifestEntry& entry,
                                       MenuDashboardSchema& out_schema)
{
    if (key_matches(entry.key, "region.menu.app_grid.height_pct"))
    {
        return parse_percent_value(entry.value, out_schema.app_grid_height_pct) &&
               (out_schema.has_app_grid_height_pct = true);
    }
    if (key_matches(entry.key, "region.menu.app_grid.top_offset"))
    {
        return parse_int16_value(entry.value, out_schema.app_grid_top_offset) &&
               (out_schema.has_app_grid_top_offset = true);
    }
    if (key_matches(entry.key, "region.menu.app_grid.pad_row"))
    {
        return parse_int16_value(entry.value, out_schema.app_grid_pad_row) &&
               (out_schema.has_app_grid_pad_row = true);
    }
    if (key_matches(entry.key, "region.menu.app_grid.pad_column"))
    {
        return parse_int16_value(entry.value, out_schema.app_grid_pad_column) &&
               (out_schema.has_app_grid_pad_column = true);
    }
    if (key_matches(entry.key, "region.menu.app_grid.pad_left"))
    {
        return parse_int16_value(entry.value, out_schema.app_grid_pad_left) &&
               (out_schema.has_app_grid_pad_left = true);
    }
    if (key_matches(entry.key, "region.menu.app_grid.pad_right"))
    {
        return parse_int16_value(entry.value, out_schema.app_grid_pad_right) &&
               (out_schema.has_app_grid_pad_right = true);
    }
    if (key_matches(entry.key, "region.menu.app_grid.align"))
    {
        SchemaAlign align = out_schema.app_grid_align;
        if (!parse_schema_align(entry.value, align))
        {
            return false;
        }
        out_schema.has_app_grid_align = true;
        out_schema.app_grid_align = align;
        return true;
    }
    if (key_matches(entry.key, "region.menu.bottom_chips.height"))
    {
        return parse_int16_value(entry.value, out_schema.bottom_chips_height) &&
               (out_schema.has_bottom_chips_height = true);
    }
    if (key_matches(entry.key, "region.menu.bottom_chips.pad_left"))
    {
        return parse_int16_value(entry.value, out_schema.bottom_chips_pad_left) &&
               (out_schema.has_bottom_chips_pad_left = true);
    }
    if (key_matches(entry.key, "region.menu.bottom_chips.pad_right"))
    {
        return parse_int16_value(entry.value, out_schema.bottom_chips_pad_right) &&
               (out_schema.has_bottom_chips_pad_right = true);
    }
    if (key_matches(entry.key, "region.menu.bottom_chips.pad_column"))
    {
        return parse_int16_value(entry.value, out_schema.bottom_chips_pad_column) &&
               (out_schema.has_bottom_chips_pad_column = true);
    }

    return false;
}

bool is_menu_dashboard_schema_key(const std::string& key)
{
    return key == "archetype" || key == "schema_version" ||
           key == "variant" || key == "layout" || key == "renderer_variant" ||
           key.rfind("layout.", 0) == 0 || key.rfind("region.menu.", 0) == 0;
}

bool apply_menu_dashboard_layout_file(const std::string& path,
                                      MenuDashboardLayout& out_layout,
                                      bool& out_layout_present,
                                      MenuDashboardSchema& out_schema,
                                      bool& out_schema_present)
{
    out_layout_present = false;
    out_schema_present = false;

    Manifest layout_manifest;
    if (!parse_manifest_file(path, layout_manifest))
    {
        return false;
    }

    MenuDashboardLayout parsed_layout = out_layout;
    MenuDashboardSchema parsed_schema{};
    bool saw_variant = false;
    bool saw_schema = false;
    bool saw_archetype = false;

    for (const ManifestEntry& entry : layout_manifest)
    {
        if (entry.key == "variant" || entry.key == "layout" ||
            entry.key == "renderer_variant")
        {
            MenuDashboardLayout candidate = parsed_layout;
            if (!parse_menu_dashboard_variant(entry.value, candidate))
            {
                return false;
            }
            parsed_layout = candidate;
            saw_variant = true;
            continue;
        }

        if (entry.key == "schema_version")
        {
            if (!parse_schema_version(entry.value))
            {
                return false;
            }
            continue;
        }

        if (entry.key == "archetype")
        {
            PageArchetype archetype = PageArchetype::MenuDashboard;
            if (!page_archetype_from_id(entry.value.c_str(), archetype) ||
                archetype != PageArchetype::MenuDashboard)
            {
                return false;
            }
            saw_archetype = true;
            continue;
        }

        if (entry.key.rfind("layout.", 0) == 0 || entry.key.rfind("region.menu.", 0) == 0)
        {
            if (!apply_menu_dashboard_schema_field(entry, parsed_schema))
            {
                return false;
            }
            saw_schema = true;
            continue;
        }

        if (is_menu_dashboard_schema_key(entry.key))
        {
            return false;
        }
    }

    if (saw_schema && !saw_archetype)
    {
        return false;
    }

    out_layout = parsed_layout;
    out_layout_present = saw_variant || saw_schema;
    out_schema = parsed_schema;
    out_schema_present = saw_schema;
    return out_layout_present || out_schema_present;
}

bool apply_boot_splash_schema_field(const ManifestEntry& entry,
                                    BootSplashSchema& out_schema)
{
    if (key_matches(entry.key, "region.boot.hero.offset_x"))
    {
        return parse_int16_value(entry.value, out_schema.hero_offset_x) &&
               (out_schema.has_hero_offset_x = true);
    }
    if (key_matches(entry.key, "region.boot.hero.offset_y"))
    {
        return parse_int16_value(entry.value, out_schema.hero_offset_y) &&
               (out_schema.has_hero_offset_y = true);
    }
    if (key_matches(entry.key, "region.boot.log.inset_x"))
    {
        return parse_int16_value(entry.value, out_schema.log_inset_x) &&
               (out_schema.has_log_inset_x = true);
    }
    if (key_matches(entry.key, "region.boot.log.bottom_inset"))
    {
        return parse_int16_value(entry.value, out_schema.log_bottom_inset) &&
               (out_schema.has_log_bottom_inset = true);
    }
    if (key_matches(entry.key, "region.boot.log.align"))
    {
        SchemaAlign align = out_schema.log_align;
        if (!parse_schema_align(entry.value, align))
        {
            return false;
        }
        out_schema.has_log_align = true;
        out_schema.log_align = align;
        return true;
    }

    return false;
}

bool is_boot_splash_schema_key(const std::string& key)
{
    return key == "archetype" || key == "schema_version" ||
           key == "variant" || key == "layout" || key == "renderer_variant" ||
           key.rfind("region.boot.", 0) == 0;
}

bool apply_boot_splash_layout_file(const std::string& path,
                                   BootSplashLayout& out_layout,
                                   bool& out_layout_present,
                                   BootSplashSchema& out_schema,
                                   bool& out_schema_present)
{
    out_layout_present = false;
    out_schema_present = false;

    Manifest layout_manifest;
    if (!parse_manifest_file(path, layout_manifest))
    {
        return false;
    }

    BootSplashLayout parsed_layout = out_layout;
    BootSplashSchema parsed_schema{};
    bool saw_variant = false;
    bool saw_schema = false;
    bool saw_archetype = false;

    for (const ManifestEntry& entry : layout_manifest)
    {
        if (entry.key == "variant" || entry.key == "layout" ||
            entry.key == "renderer_variant")
        {
            BootSplashLayout candidate = parsed_layout;
            if (!parse_boot_splash_variant(entry.value, candidate))
            {
                return false;
            }
            parsed_layout = candidate;
            saw_variant = true;
            continue;
        }

        if (entry.key == "schema_version")
        {
            if (!parse_schema_version(entry.value))
            {
                return false;
            }
            continue;
        }

        if (entry.key == "archetype")
        {
            PageArchetype archetype = PageArchetype::BootSplash;
            if (!page_archetype_from_id(entry.value.c_str(), archetype) ||
                archetype != PageArchetype::BootSplash)
            {
                return false;
            }
            saw_archetype = true;
            continue;
        }

        if (entry.key.rfind("region.boot.", 0) == 0)
        {
            if (!apply_boot_splash_schema_field(entry, parsed_schema))
            {
                return false;
            }
            saw_schema = true;
            continue;
        }

        if (is_boot_splash_schema_key(entry.key))
        {
            return false;
        }
    }

    if (saw_schema && !saw_archetype)
    {
        return false;
    }

    out_layout = parsed_layout;
    out_layout_present = saw_variant || saw_schema;
    out_schema = parsed_schema;
    out_schema_present = saw_schema;
    return out_layout_present || out_schema_present;
}

bool apply_watch_face_schema_field(const ManifestEntry& entry,
                                   WatchFaceSchema& out_schema)
{
    if (key_matches(entry.key,
                    "region.watchface.primary.width_pct",
                    "region.watch_face.primary.width_pct"))
    {
        return parse_percent_value(entry.value, out_schema.primary_width_pct) &&
               (out_schema.has_primary_width_pct = true);
    }
    if (key_matches(entry.key,
                    "region.watchface.primary.height_pct",
                    "region.watch_face.primary.height_pct"))
    {
        return parse_percent_value(entry.value, out_schema.primary_height_pct) &&
               (out_schema.has_primary_height_pct = true);
    }
    if (key_matches(entry.key,
                    "region.watchface.primary.offset_x",
                    "region.watch_face.primary.offset_x"))
    {
        return parse_int16_value(entry.value, out_schema.primary_offset_x) &&
               (out_schema.has_primary_offset_x = true);
    }
    if (key_matches(entry.key,
                    "region.watchface.primary.offset_y",
                    "region.watch_face.primary.offset_y"))
    {
        return parse_int16_value(entry.value, out_schema.primary_offset_y) &&
               (out_schema.has_primary_offset_y = true);
    }

    return false;
}

bool is_watch_face_schema_key(const std::string& key)
{
    return key == "archetype" || key == "schema_version" ||
           key == "variant" || key == "layout" || key == "renderer_variant" ||
           key.rfind("region.watchface.", 0) == 0 || key.rfind("region.watch_face.", 0) == 0;
}

bool apply_watch_face_layout_file(const std::string& path,
                                  WatchFaceLayout& out_layout,
                                  bool& out_layout_present,
                                  WatchFaceSchema& out_schema,
                                  bool& out_schema_present)
{
    out_layout_present = false;
    out_schema_present = false;

    Manifest layout_manifest;
    if (!parse_manifest_file(path, layout_manifest))
    {
        return false;
    }

    WatchFaceLayout parsed_layout = out_layout;
    WatchFaceSchema parsed_schema{};
    bool saw_variant = false;
    bool saw_schema = false;
    bool saw_archetype = false;

    for (const ManifestEntry& entry : layout_manifest)
    {
        if (entry.key == "variant" || entry.key == "layout" ||
            entry.key == "renderer_variant")
        {
            WatchFaceLayout candidate = parsed_layout;
            if (!parse_watch_face_variant(entry.value, candidate))
            {
                return false;
            }
            parsed_layout = candidate;
            saw_variant = true;
            continue;
        }

        if (entry.key == "schema_version")
        {
            if (!parse_schema_version(entry.value))
            {
                return false;
            }
            continue;
        }

        if (entry.key == "archetype")
        {
            PageArchetype archetype = PageArchetype::WatchFace;
            if (!page_archetype_from_id(entry.value.c_str(), archetype) ||
                archetype != PageArchetype::WatchFace)
            {
                return false;
            }
            saw_archetype = true;
            continue;
        }

        if (entry.key.rfind("region.watchface.", 0) == 0 ||
            entry.key.rfind("region.watch_face.", 0) == 0)
        {
            if (!apply_watch_face_schema_field(entry, parsed_schema))
            {
                return false;
            }
            saw_schema = true;
            continue;
        }

        if (is_watch_face_schema_key(entry.key))
        {
            return false;
        }
    }

    if (saw_schema && !saw_archetype)
    {
        return false;
    }

    out_layout = parsed_layout;
    out_layout_present = saw_variant || saw_schema;
    out_schema = parsed_schema;
    out_schema_present = saw_schema;
    return out_layout_present || out_schema_present;
}

bool parse_chat_list_variant(const std::string& value, ChatListLayout& out_layout)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    if (normalized == "conversation-directory" || normalized == "conversation_directory" ||
        normalized == "conversation-list" || normalized == "conversation_list" ||
        normalized == "default" || normalized == "standard" ||
        normalized == "official-default")
    {
        out_layout = ChatListLayout::ConversationDirectory;
        return true;
    }
    return false;
}

bool parse_directory_browser_variant(const std::string& value, DirectoryBrowserLayout& out_layout)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    if (normalized == "split-sidebar" || normalized == "split_sidebar" ||
        normalized == "split" || normalized == "builtin-default" ||
        normalized == "official-default")
    {
        out_layout = DirectoryBrowserLayout::SplitSidebar;
        return true;
    }
    if (normalized == "stacked-selectors" || normalized == "stacked_selectors" ||
        normalized == "stacked" || normalized == "builtin-directory-stacked" ||
        normalized == "official-directory-stacked")
    {
        out_layout = DirectoryBrowserLayout::StackedSelectors;
        return true;
    }
    return false;
}

bool parse_directory_browser_axis(const std::string& value, DirectoryBrowserAxis& out_axis)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    if (normalized == "row" || normalized == "horizontal" || normalized == "split")
    {
        out_axis = DirectoryBrowserAxis::Row;
        return true;
    }
    if (normalized == "column" || normalized == "vertical" || normalized == "stacked")
    {
        out_axis = DirectoryBrowserAxis::Column;
        return true;
    }
    return false;
}

bool parse_directory_browser_region_position(const std::string& value, bool& out_selector_first)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    if (normalized == "first" || normalized == "primary" || normalized == "start")
    {
        out_selector_first = true;
        return true;
    }
    if (normalized == "last" || normalized == "secondary" || normalized == "end")
    {
        out_selector_first = false;
        return true;
    }
    return false;
}

bool parse_schema_version(const std::string& value)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    return normalized.empty() || normalized == "1" || normalized == "v1";
}

bool key_matches(const std::string& key,
                 const char* canonical,
                 const char* alias1,
                 const char* alias2)
{
    if (key == canonical)
    {
        return true;
    }
    if (alias1 && key == alias1)
    {
        return true;
    }
    if (alias2 && key == alias2)
    {
        return true;
    }
    return false;
}

bool apply_directory_browser_schema_field(const ManifestEntry& entry,
                                          DirectoryBrowserSchema& out_schema)
{
    if (key_matches(entry.key, "layout.axis"))
    {
        DirectoryBrowserAxis axis = out_schema.axis;
        if (!parse_directory_browser_axis(entry.value, axis))
        {
            return false;
        }
        out_schema.has_axis = true;
        out_schema.axis = axis;
        return true;
    }

    if (key_matches(entry.key,
                    "region.directory.selector.position",
                    "region.directory_selector.position"))
    {
        bool selector_first = out_schema.selector_first;
        if (!parse_directory_browser_region_position(entry.value, selector_first))
        {
            return false;
        }
        out_schema.has_selector_first = true;
        out_schema.selector_first = selector_first;
        return true;
    }

    if (key_matches(entry.key,
                    "region.directory.selector.width",
                    "region.directory_selector.width"))
    {
        return parse_int16_value(entry.value, out_schema.selector_width) &&
               (out_schema.has_selector_width = true);
    }
    if (key_matches(entry.key,
                    "region.directory.selector.pad_all",
                    "region.directory_selector.pad_all"))
    {
        return parse_int16_value(entry.value, out_schema.selector_pad_all) &&
               (out_schema.has_selector_pad_all = true);
    }
    if (key_matches(entry.key,
                    "region.directory.selector.pad_row",
                    "region.directory_selector.pad_row"))
    {
        return parse_int16_value(entry.value, out_schema.selector_pad_row) &&
               (out_schema.has_selector_pad_row = true);
    }
    if (key_matches(entry.key,
                    "region.directory.selector.gap_row",
                    "region.directory_selector.gap_row"))
    {
        return parse_int16_value(entry.value, out_schema.selector_gap_row) &&
               (out_schema.has_selector_gap_row = true);
    }
    if (key_matches(entry.key,
                    "region.directory.selector.gap_column",
                    "region.directory_selector.gap_column"))
    {
        return parse_int16_value(entry.value, out_schema.selector_gap_column) &&
               (out_schema.has_selector_gap_column = true);
    }
    if (key_matches(entry.key,
                    "region.directory.selector.margin_after",
                    "region.directory_selector.margin_after"))
    {
        return parse_int16_value(entry.value, out_schema.selector_margin_after) &&
               (out_schema.has_selector_margin_after = true);
    }

    if (key_matches(entry.key,
                    "region.directory.content.pad_all",
                    "region.directory_content.pad_all"))
    {
        return parse_int16_value(entry.value, out_schema.content_pad_all) &&
               (out_schema.has_content_pad_all = true);
    }
    if (key_matches(entry.key,
                    "region.directory.content.pad_row",
                    "region.directory_content.pad_row"))
    {
        return parse_int16_value(entry.value, out_schema.content_pad_row) &&
               (out_schema.has_content_pad_row = true);
    }
    if (key_matches(entry.key,
                    "region.directory.content.pad_left",
                    "region.directory_content.pad_left"))
    {
        return parse_int16_value(entry.value, out_schema.content_pad_left) &&
               (out_schema.has_content_pad_left = true);
    }
    if (key_matches(entry.key,
                    "region.directory.content.pad_right",
                    "region.directory_content.pad_right"))
    {
        return parse_int16_value(entry.value, out_schema.content_pad_right) &&
               (out_schema.has_content_pad_right = true);
    }
    if (key_matches(entry.key,
                    "region.directory.content.pad_top",
                    "region.directory_content.pad_top"))
    {
        return parse_int16_value(entry.value, out_schema.content_pad_top) &&
               (out_schema.has_content_pad_top = true);
    }
    if (key_matches(entry.key,
                    "region.directory.content.pad_bottom",
                    "region.directory_content.pad_bottom"))
    {
        return parse_int16_value(entry.value, out_schema.content_pad_bottom) &&
               (out_schema.has_content_pad_bottom = true);
    }

    if (key_matches(entry.key,
                    "component.directory.selector_button.height",
                    "component.directory_selector_button.height"))
    {
        return parse_int16_value(entry.value, out_schema.selector_button_height) &&
               (out_schema.has_selector_button_height = true);
    }
    if (key_matches(entry.key,
                    "component.directory.selector_button.stacked_min_width",
                    "component.directory_selector_button.stacked_min_width"))
    {
        return parse_int16_value(entry.value,
                                 out_schema.selector_button_stacked_min_width) &&
               (out_schema.has_selector_button_stacked_min_width = true);
    }
    if (key_matches(entry.key,
                    "component.directory.selector_button.stacked_flex_grow",
                    "component.directory_selector_button.stacked_flex_grow"))
    {
        return parse_bool_value(entry.value,
                                out_schema.selector_button_stacked_flex_grow) &&
               (out_schema.has_selector_button_stacked_flex_grow = true);
    }

    return false;
}

bool is_directory_browser_schema_key(const std::string& key)
{
    return key == "archetype" || key == "schema_version" ||
           key == "variant" || key == "layout" || key == "renderer_variant" ||
           key.rfind("layout.", 0) == 0 || key.rfind("region.", 0) == 0 ||
           key.rfind("component.", 0) == 0;
}

bool apply_directory_browser_layout_file(const std::string& path,
                                         DirectoryBrowserLayout& out_layout,
                                         bool& out_layout_present,
                                         DirectoryBrowserSchema& out_schema,
                                         bool& out_schema_present)
{
    out_layout_present = false;
    out_schema_present = false;

    Manifest layout_manifest;
    if (!parse_manifest_file(path, layout_manifest))
    {
        return false;
    }

    DirectoryBrowserLayout parsed_layout = out_layout;
    DirectoryBrowserSchema parsed_schema{};
    bool saw_variant = false;
    bool saw_schema = false;
    bool saw_archetype = false;

    for (const ManifestEntry& entry : layout_manifest)
    {
        if (entry.key == "variant" || entry.key == "layout" ||
            entry.key == "renderer_variant")
        {
            DirectoryBrowserLayout candidate = parsed_layout;
            if (!parse_directory_browser_variant(entry.value, candidate))
            {
                return false;
            }
            parsed_layout = candidate;
            saw_variant = true;
            continue;
        }

        if (entry.key == "schema_version")
        {
            if (!parse_schema_version(entry.value))
            {
                return false;
            }
            continue;
        }

        if (entry.key == "archetype")
        {
            PageArchetype archetype = PageArchetype::DirectoryBrowser;
            if (!page_archetype_from_id(entry.value.c_str(), archetype) ||
                archetype != PageArchetype::DirectoryBrowser)
            {
                return false;
            }
            saw_archetype = true;
            continue;
        }

        if (entry.key.rfind("layout.", 0) == 0 || entry.key.rfind("region.", 0) == 0 ||
            entry.key.rfind("component.", 0) == 0)
        {
            if (!apply_directory_browser_schema_field(entry, parsed_schema))
            {
                return false;
            }
            saw_schema = true;
            continue;
        }

        if (is_directory_browser_schema_key(entry.key))
        {
            return false;
        }
    }

    if (saw_schema && !saw_archetype)
    {
        return false;
    }

    if (parsed_schema.has_axis)
    {
        const DirectoryBrowserLayout axis_layout =
            parsed_schema.axis == DirectoryBrowserAxis::Column
                ? DirectoryBrowserLayout::StackedSelectors
                : DirectoryBrowserLayout::SplitSidebar;
        if (saw_variant && parsed_layout != axis_layout)
        {
            return false;
        }
        parsed_layout = axis_layout;
    }

    out_layout = parsed_layout;
    out_layout_present = saw_variant || parsed_schema.has_axis;
    out_schema = parsed_schema;
    out_schema_present = saw_schema;
    return out_layout_present || out_schema_present;
}

bool parse_chat_conversation_variant(const std::string& value, ChatConversationLayout& out_layout)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    if (normalized == "thread-action-bar" || normalized == "thread_action_bar" ||
        normalized == "default" || normalized == "standard" ||
        normalized == "official-default")
    {
        out_layout = ChatConversationLayout::Standard;
        return true;
    }
    return false;
}

bool parse_chat_compose_variant(const std::string& value, ChatComposeLayout& out_layout)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    if (normalized == "composer-action-bar" || normalized == "composer_action_bar" ||
        normalized == "default" || normalized == "standard" ||
        normalized == "official-default")
    {
        out_layout = ChatComposeLayout::Standard;
        return true;
    }
    return false;
}

bool parse_map_focus_variant(const std::string& value, MapFocusLayout& out_layout)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    if (normalized == "overlay-panels" || normalized == "overlay_panels" ||
        normalized == "default" || normalized == "standard" ||
        normalized == "official-default")
    {
        out_layout = MapFocusLayout::OverlayPanels;
        return true;
    }
    return false;
}

bool apply_map_focus_schema_field(const ManifestEntry& entry, MapFocusSchema& out_schema)
{
    if (key_matches(entry.key, "layout.row_gap"))
    {
        return parse_int16_value(entry.value, out_schema.root_pad_row) &&
               (out_schema.has_root_pad_row = true);
    }
    if (key_matches(entry.key, "region.map.overlay.primary.position"))
    {
        SchemaCorner corner = out_schema.primary_overlay_position;
        if (!parse_schema_corner(entry.value, corner))
        {
            return false;
        }
        out_schema.has_primary_overlay_position = true;
        out_schema.primary_overlay_position = corner;
        return true;
    }
    if (key_matches(entry.key, "region.map.overlay.primary.width"))
    {
        return parse_int16_value(entry.value, out_schema.primary_overlay_width) &&
               (out_schema.has_primary_overlay_width = true);
    }
    if (key_matches(entry.key, "region.map.overlay.primary.offset_x"))
    {
        return parse_int16_value(entry.value, out_schema.primary_overlay_offset_x) &&
               (out_schema.has_primary_overlay_offset_x = true);
    }
    if (key_matches(entry.key, "region.map.overlay.primary.offset_y"))
    {
        return parse_int16_value(entry.value, out_schema.primary_overlay_offset_y) &&
               (out_schema.has_primary_overlay_offset_y = true);
    }
    if (key_matches(entry.key, "region.map.overlay.primary.gap_row"))
    {
        return parse_int16_value(entry.value, out_schema.primary_overlay_gap_row) &&
               (out_schema.has_primary_overlay_gap_row = true);
    }
    if (key_matches(entry.key, "region.map.overlay.primary.gap_column"))
    {
        return parse_int16_value(entry.value, out_schema.primary_overlay_gap_column) &&
               (out_schema.has_primary_overlay_gap_column = true);
    }
    if (key_matches(entry.key, "region.map.overlay.primary.flow"))
    {
        SchemaFlow flow = out_schema.primary_overlay_flow;
        if (!parse_schema_flow(entry.value, flow))
        {
            return false;
        }
        out_schema.has_primary_overlay_flow = true;
        out_schema.primary_overlay_flow = flow;
        return true;
    }
    if (key_matches(entry.key, "region.map.overlay.secondary.position"))
    {
        SchemaCorner corner = out_schema.secondary_overlay_position;
        if (!parse_schema_corner(entry.value, corner))
        {
            return false;
        }
        out_schema.has_secondary_overlay_position = true;
        out_schema.secondary_overlay_position = corner;
        return true;
    }
    if (key_matches(entry.key, "region.map.overlay.secondary.width"))
    {
        return parse_int16_value(entry.value, out_schema.secondary_overlay_width) &&
               (out_schema.has_secondary_overlay_width = true);
    }
    if (key_matches(entry.key, "region.map.overlay.secondary.offset_x"))
    {
        return parse_int16_value(entry.value, out_schema.secondary_overlay_offset_x) &&
               (out_schema.has_secondary_overlay_offset_x = true);
    }
    if (key_matches(entry.key, "region.map.overlay.secondary.offset_y"))
    {
        return parse_int16_value(entry.value, out_schema.secondary_overlay_offset_y) &&
               (out_schema.has_secondary_overlay_offset_y = true);
    }
    if (key_matches(entry.key, "region.map.overlay.secondary.gap_row"))
    {
        return parse_int16_value(entry.value, out_schema.secondary_overlay_gap_row) &&
               (out_schema.has_secondary_overlay_gap_row = true);
    }
    if (key_matches(entry.key, "region.map.overlay.secondary.gap_column"))
    {
        return parse_int16_value(entry.value, out_schema.secondary_overlay_gap_column) &&
               (out_schema.has_secondary_overlay_gap_column = true);
    }
    if (key_matches(entry.key, "region.map.overlay.secondary.flow"))
    {
        SchemaFlow flow = out_schema.secondary_overlay_flow;
        if (!parse_schema_flow(entry.value, flow))
        {
            return false;
        }
        out_schema.has_secondary_overlay_flow = true;
        out_schema.secondary_overlay_flow = flow;
        return true;
    }
    return false;
}

bool is_map_focus_schema_key(const std::string& key)
{
    return key_matches(key, "layout.row_gap") ||
           key_matches(key, "region.map.overlay.primary.position") ||
           key_matches(key, "region.map.overlay.primary.width") ||
           key_matches(key, "region.map.overlay.primary.offset_x") ||
           key_matches(key, "region.map.overlay.primary.offset_y") ||
           key_matches(key, "region.map.overlay.primary.gap_row") ||
           key_matches(key, "region.map.overlay.primary.gap_column") ||
           key_matches(key, "region.map.overlay.primary.flow") ||
           key_matches(key, "region.map.overlay.secondary.position") ||
           key_matches(key, "region.map.overlay.secondary.width") ||
           key_matches(key, "region.map.overlay.secondary.offset_x") ||
           key_matches(key, "region.map.overlay.secondary.offset_y") ||
           key_matches(key, "region.map.overlay.secondary.gap_row") ||
           key_matches(key, "region.map.overlay.secondary.gap_column") ||
           key_matches(key, "region.map.overlay.secondary.flow");
}

bool apply_map_focus_layout_file(const std::string& path,
                                 MapFocusLayout& out_layout,
                                 bool& out_layout_present,
                                 MapFocusSchema& out_schema,
                                 bool& out_schema_present)
{
    Manifest manifest;
    if (!parse_manifest_file(path, manifest))
    {
        return false;
    }

    bool saw_layout = false;
    bool saw_schema = false;
    MapFocusLayout parsed_layout = out_layout;
    MapFocusSchema parsed_schema{};

    for (const ManifestEntry& entry : manifest)
    {
        if (entry.key == "archetype")
        {
            if (lowercase_ascii(trim_copy(entry.value)) != "page_archetype.map_focus")
            {
                return false;
            }
            continue;
        }
        if (entry.key == "schema_version")
        {
            if (!parse_schema_version(entry.value))
            {
                return false;
            }
            continue;
        }
        if (entry.key == "variant" || entry.key == "layout" || entry.key == "renderer_variant")
        {
            if (!parse_map_focus_variant(entry.value, parsed_layout))
            {
                return false;
            }
            saw_layout = true;
            continue;
        }
        if (is_map_focus_schema_key(entry.key))
        {
            if (!apply_map_focus_schema_field(entry, parsed_schema))
            {
                return false;
            }
            saw_schema = true;
            continue;
        }
        return false;
    }

    if (saw_layout)
    {
        out_layout = parsed_layout;
    }
    out_layout_present = saw_layout;
    out_schema = parsed_schema;
    out_schema_present = saw_schema;
    return out_layout_present || out_schema_present;
}

bool parse_instrument_panel_variant(const std::string& value, InstrumentPanelLayout& out_layout)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    if (normalized == "split-canvas-legend" || normalized == "split_canvas_legend" ||
        normalized == "default" || normalized == "standard" ||
        normalized == "official-default")
    {
        out_layout = InstrumentPanelLayout::SplitCanvasLegend;
        return true;
    }
    return false;
}

bool parse_service_panel_variant(const std::string& value, ServicePanelLayout& out_layout)
{
    const std::string normalized = lowercase_ascii(trim_copy(value));
    if (normalized == "primary-panel-footer" || normalized == "primary_panel_footer" ||
        normalized == "default" || normalized == "standard" ||
        normalized == "official-default")
    {
        out_layout = ServicePanelLayout::PrimaryPanelWithFooter;
        return true;
    }
    return false;
}

bool apply_instrument_panel_schema_field(const ManifestEntry& entry,
                                         InstrumentPanelSchema& out_schema)
{
    if (key_matches(entry.key, "layout.row_gap"))
    {
        return parse_int16_value(entry.value, out_schema.root_pad_row) &&
               (out_schema.has_root_pad_row = true);
    }
    if (key_matches(entry.key, "layout.body.flow"))
    {
        SchemaFlow flow = out_schema.body_flow;
        if (!parse_schema_flow(entry.value, flow))
        {
            return false;
        }
        out_schema.has_body_flow = true;
        out_schema.body_flow = flow;
        return true;
    }
    if (key_matches(entry.key, "layout.body.pad_all"))
    {
        return parse_int16_value(entry.value, out_schema.body_pad_all) &&
               (out_schema.has_body_pad_all = true);
    }
    if (key_matches(entry.key, "layout.body.pad_row"))
    {
        return parse_int16_value(entry.value, out_schema.body_pad_row) &&
               (out_schema.has_body_pad_row = true);
    }
    if (key_matches(entry.key, "layout.body.pad_column"))
    {
        return parse_int16_value(entry.value, out_schema.body_pad_column) &&
               (out_schema.has_body_pad_column = true);
    }
    if (key_matches(entry.key, "region.instrument.canvas.grow"))
    {
        return parse_bool_value(entry.value, out_schema.canvas_grow) &&
               (out_schema.has_canvas_grow = true);
    }
    if (key_matches(entry.key, "region.instrument.canvas.pad_all"))
    {
        return parse_int16_value(entry.value, out_schema.canvas_pad_all) &&
               (out_schema.has_canvas_pad_all = true);
    }
    if (key_matches(entry.key, "region.instrument.canvas.pad_row"))
    {
        return parse_int16_value(entry.value, out_schema.canvas_pad_row) &&
               (out_schema.has_canvas_pad_row = true);
    }
    if (key_matches(entry.key, "region.instrument.canvas.pad_column"))
    {
        return parse_int16_value(entry.value, out_schema.canvas_pad_column) &&
               (out_schema.has_canvas_pad_column = true);
    }
    if (key_matches(entry.key, "region.instrument.legend.grow"))
    {
        return parse_bool_value(entry.value, out_schema.legend_grow) &&
               (out_schema.has_legend_grow = true);
    }
    if (key_matches(entry.key, "region.instrument.legend.pad_all"))
    {
        return parse_int16_value(entry.value, out_schema.legend_pad_all) &&
               (out_schema.has_legend_pad_all = true);
    }
    if (key_matches(entry.key, "region.instrument.legend.pad_row"))
    {
        return parse_int16_value(entry.value, out_schema.legend_pad_row) &&
               (out_schema.has_legend_pad_row = true);
    }
    if (key_matches(entry.key, "region.instrument.legend.pad_column"))
    {
        return parse_int16_value(entry.value, out_schema.legend_pad_column) &&
               (out_schema.has_legend_pad_column = true);
    }
    return false;
}

bool is_instrument_panel_schema_key(const std::string& key)
{
    return key_matches(key, "layout.row_gap") ||
           key_matches(key, "layout.body.flow") ||
           key_matches(key, "layout.body.pad_all") ||
           key_matches(key, "layout.body.pad_row") ||
           key_matches(key, "layout.body.pad_column") ||
           key_matches(key, "region.instrument.canvas.grow") ||
           key_matches(key, "region.instrument.canvas.pad_all") ||
           key_matches(key, "region.instrument.canvas.pad_row") ||
           key_matches(key, "region.instrument.canvas.pad_column") ||
           key_matches(key, "region.instrument.legend.grow") ||
           key_matches(key, "region.instrument.legend.pad_all") ||
           key_matches(key, "region.instrument.legend.pad_row") ||
           key_matches(key, "region.instrument.legend.pad_column");
}

bool apply_instrument_panel_layout_file(const std::string& path,
                                        InstrumentPanelLayout& out_layout,
                                        bool& out_layout_present,
                                        InstrumentPanelSchema& out_schema,
                                        bool& out_schema_present)
{
    Manifest manifest;
    if (!parse_manifest_file(path, manifest))
    {
        return false;
    }

    bool saw_layout = false;
    bool saw_schema = false;
    InstrumentPanelLayout parsed_layout = out_layout;
    InstrumentPanelSchema parsed_schema{};

    for (const ManifestEntry& entry : manifest)
    {
        if (entry.key == "archetype")
        {
            if (lowercase_ascii(trim_copy(entry.value)) != "page_archetype.instrument_panel")
            {
                return false;
            }
            continue;
        }
        if (entry.key == "schema_version")
        {
            if (!parse_schema_version(entry.value))
            {
                return false;
            }
            continue;
        }
        if (entry.key == "variant" || entry.key == "layout" || entry.key == "renderer_variant")
        {
            if (!parse_instrument_panel_variant(entry.value, parsed_layout))
            {
                return false;
            }
            saw_layout = true;
            continue;
        }
        if (is_instrument_panel_schema_key(entry.key))
        {
            if (!apply_instrument_panel_schema_field(entry, parsed_schema))
            {
                return false;
            }
            saw_schema = true;
            continue;
        }
        return false;
    }

    out_layout = parsed_layout;
    out_layout_present = saw_layout || saw_schema;
    out_schema = parsed_schema;
    out_schema_present = saw_schema;
    return out_layout_present || out_schema_present;
}

bool apply_service_panel_schema_field(const ManifestEntry& entry,
                                      ServicePanelSchema& out_schema)
{
    if (key_matches(entry.key, "layout.row_gap"))
    {
        return parse_int16_value(entry.value, out_schema.root_pad_row) &&
               (out_schema.has_root_pad_row = true);
    }
    if (key_matches(entry.key, "layout.body.pad_all"))
    {
        return parse_int16_value(entry.value, out_schema.body_pad_all) &&
               (out_schema.has_body_pad_all = true);
    }
    if (key_matches(entry.key, "layout.body.pad_row"))
    {
        return parse_int16_value(entry.value, out_schema.body_pad_row) &&
               (out_schema.has_body_pad_row = true);
    }
    if (key_matches(entry.key, "layout.body.pad_column"))
    {
        return parse_int16_value(entry.value, out_schema.body_pad_column) &&
               (out_schema.has_body_pad_column = true);
    }
    if (key_matches(entry.key, "region.service.primary_panel.pad_all"))
    {
        return parse_int16_value(entry.value, out_schema.primary_panel_pad_all) &&
               (out_schema.has_primary_panel_pad_all = true);
    }
    if (key_matches(entry.key, "region.service.primary_panel.pad_row"))
    {
        return parse_int16_value(entry.value, out_schema.primary_panel_pad_row) &&
               (out_schema.has_primary_panel_pad_row = true);
    }
    if (key_matches(entry.key, "region.service.primary_panel.pad_column"))
    {
        return parse_int16_value(entry.value, out_schema.primary_panel_pad_column) &&
               (out_schema.has_primary_panel_pad_column = true);
    }
    if (key_matches(entry.key, "region.service.footer_actions.height"))
    {
        return parse_int16_value(entry.value, out_schema.footer_actions_height) &&
               (out_schema.has_footer_actions_height = true);
    }
    if (key_matches(entry.key, "region.service.footer_actions.pad_all"))
    {
        return parse_int16_value(entry.value, out_schema.footer_actions_pad_all) &&
               (out_schema.has_footer_actions_pad_all = true);
    }
    if (key_matches(entry.key, "region.service.footer_actions.pad_row"))
    {
        return parse_int16_value(entry.value, out_schema.footer_actions_pad_row) &&
               (out_schema.has_footer_actions_pad_row = true);
    }
    if (key_matches(entry.key, "region.service.footer_actions.pad_column"))
    {
        return parse_int16_value(entry.value, out_schema.footer_actions_pad_column) &&
               (out_schema.has_footer_actions_pad_column = true);
    }
    return false;
}

bool is_service_panel_schema_key(const std::string& key)
{
    return key_matches(key, "layout.row_gap") ||
           key_matches(key, "layout.body.pad_all") ||
           key_matches(key, "layout.body.pad_row") ||
           key_matches(key, "layout.body.pad_column") ||
           key_matches(key, "region.service.primary_panel.pad_all") ||
           key_matches(key, "region.service.primary_panel.pad_row") ||
           key_matches(key, "region.service.primary_panel.pad_column") ||
           key_matches(key, "region.service.footer_actions.height") ||
           key_matches(key, "region.service.footer_actions.pad_all") ||
           key_matches(key, "region.service.footer_actions.pad_row") ||
           key_matches(key, "region.service.footer_actions.pad_column");
}

bool apply_service_panel_layout_file(const std::string& path,
                                     ServicePanelLayout& out_layout,
                                     bool& out_layout_present,
                                     ServicePanelSchema& out_schema,
                                     bool& out_schema_present)
{
    Manifest manifest;
    if (!parse_manifest_file(path, manifest))
    {
        return false;
    }

    bool saw_layout = false;
    bool saw_schema = false;
    ServicePanelLayout parsed_layout = out_layout;
    ServicePanelSchema parsed_schema{};

    for (const ManifestEntry& entry : manifest)
    {
        if (entry.key == "archetype")
        {
            if (lowercase_ascii(trim_copy(entry.value)) != "page_archetype.service_panel")
            {
                return false;
            }
            continue;
        }
        if (entry.key == "schema_version")
        {
            if (!parse_schema_version(entry.value))
            {
                return false;
            }
            continue;
        }
        if (entry.key == "variant" || entry.key == "layout" || entry.key == "renderer_variant")
        {
            if (!parse_service_panel_variant(entry.value, parsed_layout))
            {
                return false;
            }
            saw_layout = true;
            continue;
        }
        if (is_service_panel_schema_key(entry.key))
        {
            if (!apply_service_panel_schema_field(entry, parsed_schema))
            {
                return false;
            }
            saw_schema = true;
            continue;
        }
        return false;
    }

    out_layout = parsed_layout;
    out_layout_present = saw_layout || saw_schema;
    out_schema = parsed_schema;
    out_schema_present = saw_schema;
    return out_layout_present || out_schema_present;
}

bool apply_chat_conversation_schema_field(const ManifestEntry& entry,
                                          ChatConversationSchema& out_schema)
{
    if (key_matches(entry.key, "layout.row_gap"))
    {
        return parse_int16_value(entry.value, out_schema.root_pad_row) &&
               (out_schema.has_root_pad_row = true);
    }
    if (key_matches(entry.key, "region.chat.thread.gap_row"))
    {
        return parse_int16_value(entry.value, out_schema.thread_pad_row) &&
               (out_schema.has_thread_pad_row = true);
    }
    if (key_matches(entry.key, "region.chat.action_bar.position"))
    {
        bool action_bar_first = out_schema.action_bar_first;
        if (!parse_directory_browser_region_position(entry.value, action_bar_first))
        {
            return false;
        }
        out_schema.has_action_bar_position = true;
        out_schema.action_bar_first = action_bar_first;
        return true;
    }
    if (key_matches(entry.key, "region.chat.action_bar.height"))
    {
        return parse_int16_value(entry.value, out_schema.action_bar_height) &&
               (out_schema.has_action_bar_height = true);
    }
    if (key_matches(entry.key, "region.chat.action_bar.pad_column"))
    {
        return parse_int16_value(entry.value, out_schema.action_bar_pad_column) &&
               (out_schema.has_action_bar_pad_column = true);
    }
    if (key_matches(entry.key, "region.chat.action_bar.align"))
    {
        SchemaAlign align = out_schema.action_bar_align;
        if (!parse_schema_align(entry.value, align))
        {
            return false;
        }
        out_schema.has_action_bar_align = true;
        out_schema.action_bar_align = align;
        return true;
    }
    if (key_matches(entry.key, "component.action_button.primary.width"))
    {
        return parse_int16_value(entry.value, out_schema.primary_button_width) &&
               (out_schema.has_primary_button_width = true);
    }
    if (key_matches(entry.key, "component.action_button.primary.height"))
    {
        return parse_int16_value(entry.value, out_schema.primary_button_height) &&
               (out_schema.has_primary_button_height = true);
    }

    return false;
}

bool is_chat_conversation_schema_key(const std::string& key)
{
    return key == "archetype" || key == "schema_version" ||
           key == "variant" || key == "layout" || key == "renderer_variant" ||
           key.rfind("layout.", 0) == 0 || key.rfind("region.chat.", 0) == 0 ||
           key.rfind("component.action_button.", 0) == 0;
}

bool apply_chat_conversation_layout_file(const std::string& path,
                                         ChatConversationLayout& out_layout,
                                         bool& out_layout_present,
                                         ChatConversationSchema& out_schema,
                                         bool& out_schema_present)
{
    out_layout_present = false;
    out_schema_present = false;

    Manifest layout_manifest;
    if (!parse_manifest_file(path, layout_manifest))
    {
        return false;
    }

    ChatConversationLayout parsed_layout = out_layout;
    ChatConversationSchema parsed_schema{};
    bool saw_variant = false;
    bool saw_schema = false;
    bool saw_archetype = false;

    for (const ManifestEntry& entry : layout_manifest)
    {
        if (entry.key == "variant" || entry.key == "layout" ||
            entry.key == "renderer_variant")
        {
            ChatConversationLayout candidate = parsed_layout;
            if (!parse_chat_conversation_variant(entry.value, candidate))
            {
                return false;
            }
            parsed_layout = candidate;
            saw_variant = true;
            continue;
        }

        if (entry.key == "schema_version")
        {
            if (!parse_schema_version(entry.value))
            {
                return false;
            }
            continue;
        }

        if (entry.key == "archetype")
        {
            PageArchetype archetype = PageArchetype::ChatConversation;
            if (!page_archetype_from_id(entry.value.c_str(), archetype) ||
                archetype != PageArchetype::ChatConversation)
            {
                return false;
            }
            saw_archetype = true;
            continue;
        }

        if (entry.key.rfind("layout.", 0) == 0 || entry.key.rfind("region.chat.", 0) == 0 ||
            entry.key.rfind("component.action_button.", 0) == 0)
        {
            if (!apply_chat_conversation_schema_field(entry, parsed_schema))
            {
                return false;
            }
            saw_schema = true;
            continue;
        }

        if (is_chat_conversation_schema_key(entry.key))
        {
            return false;
        }
    }

    if (saw_schema && !saw_archetype)
    {
        return false;
    }

    out_layout = parsed_layout;
    out_layout_present = saw_variant || saw_schema;
    out_schema = parsed_schema;
    out_schema_present = saw_schema;
    return out_layout_present || out_schema_present;
}

bool apply_chat_compose_schema_field(const ManifestEntry& entry,
                                     ChatComposeSchema& out_schema)
{
    if (key_matches(entry.key, "layout.row_gap"))
    {
        return parse_int16_value(entry.value, out_schema.root_pad_row) &&
               (out_schema.has_root_pad_row = true);
    }
    if (key_matches(entry.key, "region.chat.composer.pad_all"))
    {
        return parse_int16_value(entry.value, out_schema.content_pad_all) &&
               (out_schema.has_content_pad_all = true);
    }
    if (key_matches(entry.key, "region.chat.composer.pad_row"))
    {
        return parse_int16_value(entry.value, out_schema.content_pad_row) &&
               (out_schema.has_content_pad_row = true);
    }
    if (key_matches(entry.key, "region.chat.action_bar.position"))
    {
        bool action_bar_first = out_schema.action_bar_first;
        if (!parse_directory_browser_region_position(entry.value, action_bar_first))
        {
            return false;
        }
        out_schema.has_action_bar_position = true;
        out_schema.action_bar_first = action_bar_first;
        return true;
    }
    if (key_matches(entry.key, "region.chat.action_bar.height"))
    {
        return parse_int16_value(entry.value, out_schema.action_bar_height) &&
               (out_schema.has_action_bar_height = true);
    }
    if (key_matches(entry.key, "region.chat.action_bar.pad_left"))
    {
        return parse_int16_value(entry.value, out_schema.action_bar_pad_left) &&
               (out_schema.has_action_bar_pad_left = true);
    }
    if (key_matches(entry.key, "region.chat.action_bar.pad_right"))
    {
        return parse_int16_value(entry.value, out_schema.action_bar_pad_right) &&
               (out_schema.has_action_bar_pad_right = true);
    }
    if (key_matches(entry.key, "region.chat.action_bar.pad_top"))
    {
        return parse_int16_value(entry.value, out_schema.action_bar_pad_top) &&
               (out_schema.has_action_bar_pad_top = true);
    }
    if (key_matches(entry.key, "region.chat.action_bar.pad_bottom"))
    {
        return parse_int16_value(entry.value, out_schema.action_bar_pad_bottom) &&
               (out_schema.has_action_bar_pad_bottom = true);
    }
    if (key_matches(entry.key, "region.chat.action_bar.pad_column"))
    {
        return parse_int16_value(entry.value, out_schema.action_bar_pad_column) &&
               (out_schema.has_action_bar_pad_column = true);
    }
    if (key_matches(entry.key, "region.chat.action_bar.align"))
    {
        SchemaAlign align = out_schema.action_bar_align;
        if (!parse_schema_align(entry.value, align))
        {
            return false;
        }
        out_schema.has_action_bar_align = true;
        out_schema.action_bar_align = align;
        return true;
    }
    if (key_matches(entry.key, "component.action_button.primary.width"))
    {
        return parse_int16_value(entry.value, out_schema.primary_button_width) &&
               (out_schema.has_primary_button_width = true);
    }
    if (key_matches(entry.key, "component.action_button.primary.height"))
    {
        return parse_int16_value(entry.value, out_schema.primary_button_height) &&
               (out_schema.has_primary_button_height = true);
    }
    if (key_matches(entry.key, "component.action_button.secondary.width"))
    {
        return parse_int16_value(entry.value, out_schema.secondary_button_width) &&
               (out_schema.has_secondary_button_width = true);
    }
    if (key_matches(entry.key, "component.action_button.secondary.height"))
    {
        return parse_int16_value(entry.value, out_schema.secondary_button_height) &&
               (out_schema.has_secondary_button_height = true);
    }

    return false;
}

bool is_chat_compose_schema_key(const std::string& key)
{
    return key == "archetype" || key == "schema_version" ||
           key == "variant" || key == "layout" || key == "renderer_variant" ||
           key.rfind("layout.", 0) == 0 || key.rfind("region.chat.", 0) == 0 ||
           key.rfind("component.action_button.", 0) == 0;
}

bool apply_chat_compose_layout_file(const std::string& path,
                                    ChatComposeLayout& out_layout,
                                    bool& out_layout_present,
                                    ChatComposeSchema& out_schema,
                                    bool& out_schema_present)
{
    out_layout_present = false;
    out_schema_present = false;

    Manifest layout_manifest;
    if (!parse_manifest_file(path, layout_manifest))
    {
        return false;
    }

    ChatComposeLayout parsed_layout = out_layout;
    ChatComposeSchema parsed_schema{};
    bool saw_variant = false;
    bool saw_schema = false;
    bool saw_archetype = false;

    for (const ManifestEntry& entry : layout_manifest)
    {
        if (entry.key == "variant" || entry.key == "layout" ||
            entry.key == "renderer_variant")
        {
            ChatComposeLayout candidate = parsed_layout;
            if (!parse_chat_compose_variant(entry.value, candidate))
            {
                return false;
            }
            parsed_layout = candidate;
            saw_variant = true;
            continue;
        }

        if (entry.key == "schema_version")
        {
            if (!parse_schema_version(entry.value))
            {
                return false;
            }
            continue;
        }

        if (entry.key == "archetype")
        {
            PageArchetype archetype = PageArchetype::ChatCompose;
            if (!page_archetype_from_id(entry.value.c_str(), archetype) ||
                archetype != PageArchetype::ChatCompose)
            {
                return false;
            }
            saw_archetype = true;
            continue;
        }

        if (entry.key.rfind("layout.", 0) == 0 || entry.key.rfind("region.chat.", 0) == 0 ||
            entry.key.rfind("component.action_button.", 0) == 0)
        {
            if (!apply_chat_compose_schema_field(entry, parsed_schema))
            {
                return false;
            }
            saw_schema = true;
            continue;
        }

        if (is_chat_compose_schema_key(entry.key))
        {
            return false;
        }
    }

    if (saw_schema && !saw_archetype)
    {
        return false;
    }

    out_layout = parsed_layout;
    out_layout_present = saw_variant || saw_schema;
    out_schema = parsed_schema;
    out_schema_present = saw_schema;
    return out_layout_present || out_schema_present;
}

template <typename LayoutEnum, typename ParseFn>
bool apply_variant_layout_file(const std::string& path, LayoutEnum& out_layout, ParseFn parse_fn)
{
    Manifest layout_manifest;
    if (!parse_manifest_file(path, layout_manifest))
    {
        return false;
    }

    for (const ManifestEntry& entry : layout_manifest)
    {
        if (entry.key == "variant" || entry.key == "layout" || entry.key == "renderer_variant")
        {
            LayoutEnum parsed = out_layout;
            if (parse_fn(entry.value, parsed))
            {
                out_layout = parsed;
                return true;
            }
        }
    }

    return false;
}

template <typename LayoutEnum, typename ParseFn>
bool apply_layout_file_if_present(const std::string& pack_dir,
                                  const Manifest& manifest,
                                  const char* manifest_key,
                                  LayoutEnum& layout_slot,
                                  bool& presence_flag,
                                  ParseFn parse_fn)
{
    const char* layout_file = manifest_value(manifest, manifest_key);
    if (!layout_file || layout_file[0] == '\0')
    {
        return true;
    }

    presence_flag =
        apply_variant_layout_file(join_path(pack_dir, layout_file), layout_slot, parse_fn);
    return presence_flag;
}

void merge_menu_dashboard_schema(MenuDashboardSchema& base,
                                 const MenuDashboardSchema& overlay)
{
    if (overlay.has_app_grid_height_pct)
    {
        base.has_app_grid_height_pct = true;
        base.app_grid_height_pct = overlay.app_grid_height_pct;
    }
    if (overlay.has_app_grid_top_offset)
    {
        base.has_app_grid_top_offset = true;
        base.app_grid_top_offset = overlay.app_grid_top_offset;
    }
    if (overlay.has_app_grid_pad_row)
    {
        base.has_app_grid_pad_row = true;
        base.app_grid_pad_row = overlay.app_grid_pad_row;
    }
    if (overlay.has_app_grid_pad_column)
    {
        base.has_app_grid_pad_column = true;
        base.app_grid_pad_column = overlay.app_grid_pad_column;
    }
    if (overlay.has_app_grid_pad_left)
    {
        base.has_app_grid_pad_left = true;
        base.app_grid_pad_left = overlay.app_grid_pad_left;
    }
    if (overlay.has_app_grid_pad_right)
    {
        base.has_app_grid_pad_right = true;
        base.app_grid_pad_right = overlay.app_grid_pad_right;
    }
    if (overlay.has_app_grid_align)
    {
        base.has_app_grid_align = true;
        base.app_grid_align = overlay.app_grid_align;
    }
    if (overlay.has_bottom_chips_height)
    {
        base.has_bottom_chips_height = true;
        base.bottom_chips_height = overlay.bottom_chips_height;
    }
    if (overlay.has_bottom_chips_pad_left)
    {
        base.has_bottom_chips_pad_left = true;
        base.bottom_chips_pad_left = overlay.bottom_chips_pad_left;
    }
    if (overlay.has_bottom_chips_pad_right)
    {
        base.has_bottom_chips_pad_right = true;
        base.bottom_chips_pad_right = overlay.bottom_chips_pad_right;
    }
    if (overlay.has_bottom_chips_pad_column)
    {
        base.has_bottom_chips_pad_column = true;
        base.bottom_chips_pad_column = overlay.bottom_chips_pad_column;
    }
}

void merge_boot_splash_schema(BootSplashSchema& base, const BootSplashSchema& overlay)
{
    if (overlay.has_hero_offset_x)
    {
        base.has_hero_offset_x = true;
        base.hero_offset_x = overlay.hero_offset_x;
    }
    if (overlay.has_hero_offset_y)
    {
        base.has_hero_offset_y = true;
        base.hero_offset_y = overlay.hero_offset_y;
    }
    if (overlay.has_log_inset_x)
    {
        base.has_log_inset_x = true;
        base.log_inset_x = overlay.log_inset_x;
    }
    if (overlay.has_log_bottom_inset)
    {
        base.has_log_bottom_inset = true;
        base.log_bottom_inset = overlay.log_bottom_inset;
    }
    if (overlay.has_log_align)
    {
        base.has_log_align = true;
        base.log_align = overlay.log_align;
    }
}

void merge_watch_face_schema(WatchFaceSchema& base, const WatchFaceSchema& overlay)
{
    if (overlay.has_primary_width_pct)
    {
        base.has_primary_width_pct = true;
        base.primary_width_pct = overlay.primary_width_pct;
    }
    if (overlay.has_primary_height_pct)
    {
        base.has_primary_height_pct = true;
        base.primary_height_pct = overlay.primary_height_pct;
    }
    if (overlay.has_primary_offset_x)
    {
        base.has_primary_offset_x = true;
        base.primary_offset_x = overlay.primary_offset_x;
    }
    if (overlay.has_primary_offset_y)
    {
        base.has_primary_offset_y = true;
        base.primary_offset_y = overlay.primary_offset_y;
    }
}

void merge_directory_browser_schema(DirectoryBrowserSchema& base,
                                    const DirectoryBrowserSchema& overlay)
{
    if (overlay.has_axis)
    {
        base.has_axis = true;
        base.axis = overlay.axis;
    }
    if (overlay.has_selector_first)
    {
        base.has_selector_first = true;
        base.selector_first = overlay.selector_first;
    }
    if (overlay.has_selector_width)
    {
        base.has_selector_width = true;
        base.selector_width = overlay.selector_width;
    }
    if (overlay.has_selector_pad_all)
    {
        base.has_selector_pad_all = true;
        base.selector_pad_all = overlay.selector_pad_all;
    }
    if (overlay.has_selector_pad_row)
    {
        base.has_selector_pad_row = true;
        base.selector_pad_row = overlay.selector_pad_row;
    }
    if (overlay.has_selector_gap_row)
    {
        base.has_selector_gap_row = true;
        base.selector_gap_row = overlay.selector_gap_row;
    }
    if (overlay.has_selector_gap_column)
    {
        base.has_selector_gap_column = true;
        base.selector_gap_column = overlay.selector_gap_column;
    }
    if (overlay.has_selector_margin_after)
    {
        base.has_selector_margin_after = true;
        base.selector_margin_after = overlay.selector_margin_after;
    }
    if (overlay.has_content_pad_all)
    {
        base.has_content_pad_all = true;
        base.content_pad_all = overlay.content_pad_all;
    }
    if (overlay.has_content_pad_row)
    {
        base.has_content_pad_row = true;
        base.content_pad_row = overlay.content_pad_row;
    }
    if (overlay.has_content_pad_left)
    {
        base.has_content_pad_left = true;
        base.content_pad_left = overlay.content_pad_left;
    }
    if (overlay.has_content_pad_right)
    {
        base.has_content_pad_right = true;
        base.content_pad_right = overlay.content_pad_right;
    }
    if (overlay.has_content_pad_top)
    {
        base.has_content_pad_top = true;
        base.content_pad_top = overlay.content_pad_top;
    }
    if (overlay.has_content_pad_bottom)
    {
        base.has_content_pad_bottom = true;
        base.content_pad_bottom = overlay.content_pad_bottom;
    }
    if (overlay.has_selector_button_height)
    {
        base.has_selector_button_height = true;
        base.selector_button_height = overlay.selector_button_height;
    }
    if (overlay.has_selector_button_stacked_min_width)
    {
        base.has_selector_button_stacked_min_width = true;
        base.selector_button_stacked_min_width =
            overlay.selector_button_stacked_min_width;
    }
    if (overlay.has_selector_button_stacked_flex_grow)
    {
        base.has_selector_button_stacked_flex_grow = true;
        base.selector_button_stacked_flex_grow =
            overlay.selector_button_stacked_flex_grow;
    }
}

void merge_chat_conversation_schema(ChatConversationSchema& base,
                                    const ChatConversationSchema& overlay)
{
    if (overlay.has_root_pad_row)
    {
        base.has_root_pad_row = true;
        base.root_pad_row = overlay.root_pad_row;
    }
    if (overlay.has_thread_pad_row)
    {
        base.has_thread_pad_row = true;
        base.thread_pad_row = overlay.thread_pad_row;
    }
    if (overlay.has_action_bar_position)
    {
        base.has_action_bar_position = true;
        base.action_bar_first = overlay.action_bar_first;
    }
    if (overlay.has_action_bar_height)
    {
        base.has_action_bar_height = true;
        base.action_bar_height = overlay.action_bar_height;
    }
    if (overlay.has_action_bar_pad_column)
    {
        base.has_action_bar_pad_column = true;
        base.action_bar_pad_column = overlay.action_bar_pad_column;
    }
    if (overlay.has_action_bar_align)
    {
        base.has_action_bar_align = true;
        base.action_bar_align = overlay.action_bar_align;
    }
    if (overlay.has_primary_button_width)
    {
        base.has_primary_button_width = true;
        base.primary_button_width = overlay.primary_button_width;
    }
    if (overlay.has_primary_button_height)
    {
        base.has_primary_button_height = true;
        base.primary_button_height = overlay.primary_button_height;
    }
}

void merge_chat_compose_schema(ChatComposeSchema& base,
                               const ChatComposeSchema& overlay)
{
    if (overlay.has_root_pad_row)
    {
        base.has_root_pad_row = true;
        base.root_pad_row = overlay.root_pad_row;
    }
    if (overlay.has_content_pad_all)
    {
        base.has_content_pad_all = true;
        base.content_pad_all = overlay.content_pad_all;
    }
    if (overlay.has_content_pad_row)
    {
        base.has_content_pad_row = true;
        base.content_pad_row = overlay.content_pad_row;
    }
    if (overlay.has_action_bar_position)
    {
        base.has_action_bar_position = true;
        base.action_bar_first = overlay.action_bar_first;
    }
    if (overlay.has_action_bar_height)
    {
        base.has_action_bar_height = true;
        base.action_bar_height = overlay.action_bar_height;
    }
    if (overlay.has_action_bar_pad_left)
    {
        base.has_action_bar_pad_left = true;
        base.action_bar_pad_left = overlay.action_bar_pad_left;
    }
    if (overlay.has_action_bar_pad_right)
    {
        base.has_action_bar_pad_right = true;
        base.action_bar_pad_right = overlay.action_bar_pad_right;
    }
    if (overlay.has_action_bar_pad_top)
    {
        base.has_action_bar_pad_top = true;
        base.action_bar_pad_top = overlay.action_bar_pad_top;
    }
    if (overlay.has_action_bar_pad_bottom)
    {
        base.has_action_bar_pad_bottom = true;
        base.action_bar_pad_bottom = overlay.action_bar_pad_bottom;
    }
    if (overlay.has_action_bar_pad_column)
    {
        base.has_action_bar_pad_column = true;
        base.action_bar_pad_column = overlay.action_bar_pad_column;
    }
    if (overlay.has_action_bar_align)
    {
        base.has_action_bar_align = true;
        base.action_bar_align = overlay.action_bar_align;
    }
    if (overlay.has_primary_button_width)
    {
        base.has_primary_button_width = true;
        base.primary_button_width = overlay.primary_button_width;
    }
    if (overlay.has_primary_button_height)
    {
        base.has_primary_button_height = true;
        base.primary_button_height = overlay.primary_button_height;
    }
    if (overlay.has_secondary_button_width)
    {
        base.has_secondary_button_width = true;
        base.secondary_button_width = overlay.secondary_button_width;
    }
    if (overlay.has_secondary_button_height)
    {
        base.has_secondary_button_height = true;
        base.secondary_button_height = overlay.secondary_button_height;
    }
}

void merge_map_focus_schema(MapFocusSchema& base, const MapFocusSchema& overlay)
{
    if (overlay.has_root_pad_row)
    {
        base.has_root_pad_row = true;
        base.root_pad_row = overlay.root_pad_row;
    }
    if (overlay.has_primary_overlay_position)
    {
        base.has_primary_overlay_position = true;
        base.primary_overlay_position = overlay.primary_overlay_position;
    }
    if (overlay.has_primary_overlay_width)
    {
        base.has_primary_overlay_width = true;
        base.primary_overlay_width = overlay.primary_overlay_width;
    }
    if (overlay.has_primary_overlay_offset_x)
    {
        base.has_primary_overlay_offset_x = true;
        base.primary_overlay_offset_x = overlay.primary_overlay_offset_x;
    }
    if (overlay.has_primary_overlay_offset_y)
    {
        base.has_primary_overlay_offset_y = true;
        base.primary_overlay_offset_y = overlay.primary_overlay_offset_y;
    }
    if (overlay.has_primary_overlay_gap_row)
    {
        base.has_primary_overlay_gap_row = true;
        base.primary_overlay_gap_row = overlay.primary_overlay_gap_row;
    }
    if (overlay.has_primary_overlay_gap_column)
    {
        base.has_primary_overlay_gap_column = true;
        base.primary_overlay_gap_column = overlay.primary_overlay_gap_column;
    }
    if (overlay.has_primary_overlay_flow)
    {
        base.has_primary_overlay_flow = true;
        base.primary_overlay_flow = overlay.primary_overlay_flow;
    }
    if (overlay.has_secondary_overlay_position)
    {
        base.has_secondary_overlay_position = true;
        base.secondary_overlay_position = overlay.secondary_overlay_position;
    }
    if (overlay.has_secondary_overlay_width)
    {
        base.has_secondary_overlay_width = true;
        base.secondary_overlay_width = overlay.secondary_overlay_width;
    }
    if (overlay.has_secondary_overlay_offset_x)
    {
        base.has_secondary_overlay_offset_x = true;
        base.secondary_overlay_offset_x = overlay.secondary_overlay_offset_x;
    }
    if (overlay.has_secondary_overlay_offset_y)
    {
        base.has_secondary_overlay_offset_y = true;
        base.secondary_overlay_offset_y = overlay.secondary_overlay_offset_y;
    }
    if (overlay.has_secondary_overlay_gap_row)
    {
        base.has_secondary_overlay_gap_row = true;
        base.secondary_overlay_gap_row = overlay.secondary_overlay_gap_row;
    }
    if (overlay.has_secondary_overlay_gap_column)
    {
        base.has_secondary_overlay_gap_column = true;
        base.secondary_overlay_gap_column = overlay.secondary_overlay_gap_column;
    }
    if (overlay.has_secondary_overlay_flow)
    {
        base.has_secondary_overlay_flow = true;
        base.secondary_overlay_flow = overlay.secondary_overlay_flow;
    }
}

void merge_instrument_panel_schema(InstrumentPanelSchema& base,
                                   const InstrumentPanelSchema& overlay)
{
    if (overlay.has_root_pad_row)
    {
        base.has_root_pad_row = true;
        base.root_pad_row = overlay.root_pad_row;
    }
    if (overlay.has_body_flow)
    {
        base.has_body_flow = true;
        base.body_flow = overlay.body_flow;
    }
    if (overlay.has_body_pad_all)
    {
        base.has_body_pad_all = true;
        base.body_pad_all = overlay.body_pad_all;
    }
    if (overlay.has_body_pad_row)
    {
        base.has_body_pad_row = true;
        base.body_pad_row = overlay.body_pad_row;
    }
    if (overlay.has_body_pad_column)
    {
        base.has_body_pad_column = true;
        base.body_pad_column = overlay.body_pad_column;
    }
    if (overlay.has_canvas_grow)
    {
        base.has_canvas_grow = true;
        base.canvas_grow = overlay.canvas_grow;
    }
    if (overlay.has_canvas_pad_all)
    {
        base.has_canvas_pad_all = true;
        base.canvas_pad_all = overlay.canvas_pad_all;
    }
    if (overlay.has_canvas_pad_row)
    {
        base.has_canvas_pad_row = true;
        base.canvas_pad_row = overlay.canvas_pad_row;
    }
    if (overlay.has_canvas_pad_column)
    {
        base.has_canvas_pad_column = true;
        base.canvas_pad_column = overlay.canvas_pad_column;
    }
    if (overlay.has_legend_grow)
    {
        base.has_legend_grow = true;
        base.legend_grow = overlay.legend_grow;
    }
    if (overlay.has_legend_pad_all)
    {
        base.has_legend_pad_all = true;
        base.legend_pad_all = overlay.legend_pad_all;
    }
    if (overlay.has_legend_pad_row)
    {
        base.has_legend_pad_row = true;
        base.legend_pad_row = overlay.legend_pad_row;
    }
    if (overlay.has_legend_pad_column)
    {
        base.has_legend_pad_column = true;
        base.legend_pad_column = overlay.legend_pad_column;
    }
}

void merge_service_panel_schema(ServicePanelSchema& base,
                                const ServicePanelSchema& overlay)
{
    if (overlay.has_root_pad_row)
    {
        base.has_root_pad_row = true;
        base.root_pad_row = overlay.root_pad_row;
    }
    if (overlay.has_body_pad_all)
    {
        base.has_body_pad_all = true;
        base.body_pad_all = overlay.body_pad_all;
    }
    if (overlay.has_body_pad_row)
    {
        base.has_body_pad_row = true;
        base.body_pad_row = overlay.body_pad_row;
    }
    if (overlay.has_body_pad_column)
    {
        base.has_body_pad_column = true;
        base.body_pad_column = overlay.body_pad_column;
    }
    if (overlay.has_primary_panel_pad_all)
    {
        base.has_primary_panel_pad_all = true;
        base.primary_panel_pad_all = overlay.primary_panel_pad_all;
    }
    if (overlay.has_primary_panel_pad_row)
    {
        base.has_primary_panel_pad_row = true;
        base.primary_panel_pad_row = overlay.primary_panel_pad_row;
    }
    if (overlay.has_primary_panel_pad_column)
    {
        base.has_primary_panel_pad_column = true;
        base.primary_panel_pad_column = overlay.primary_panel_pad_column;
    }
    if (overlay.has_footer_actions_height)
    {
        base.has_footer_actions_height = true;
        base.footer_actions_height = overlay.footer_actions_height;
    }
    if (overlay.has_footer_actions_pad_all)
    {
        base.has_footer_actions_pad_all = true;
        base.footer_actions_pad_all = overlay.footer_actions_pad_all;
    }
    if (overlay.has_footer_actions_pad_row)
    {
        base.has_footer_actions_pad_row = true;
        base.footer_actions_pad_row = overlay.footer_actions_pad_row;
    }
    if (overlay.has_footer_actions_pad_column)
    {
        base.has_footer_actions_pad_column = true;
        base.footer_actions_pad_column = overlay.footer_actions_pad_column;
    }
}

void load_external_presentations_from_root(const char* root_path)
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
        if (id.empty() || find_builtin_presentation(id.c_str()) ||
            find_external_presentation(id.c_str()))
        {
            continue;
        }

        ExternalPresentationRecord record{};
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
        const char* base_presentation = manifest_value(manifest, "base_presentation");
        if (base_presentation && base_presentation[0] != '\0')
        {
            record.base_presentation_id = base_presentation;
        }

        bool valid = true;

        const char* menu_dashboard_layout_file = manifest_value(manifest, "layout.menu_dashboard");
        if (menu_dashboard_layout_file && menu_dashboard_layout_file[0] != '\0')
        {
            valid = apply_menu_dashboard_layout_file(join_path(pack_dir, menu_dashboard_layout_file),
                                                     record.layouts.menu_dashboard,
                                                     record.has_layouts.menu_dashboard,
                                                     record.menu_dashboard_schema,
                                                     record.has_menu_dashboard_schema) && valid;
        }
        const char* boot_splash_layout_file = manifest_value(manifest, "layout.boot_splash");
        if (boot_splash_layout_file && boot_splash_layout_file[0] != '\0')
        {
            valid = apply_boot_splash_layout_file(join_path(pack_dir, boot_splash_layout_file),
                                                  record.layouts.boot_splash,
                                                  record.has_layouts.boot_splash,
                                                  record.boot_splash_schema,
                                                  record.has_boot_splash_schema) && valid;
        }
        const char* watch_face_layout_file = manifest_value(manifest, "layout.watch_face");
        if (watch_face_layout_file && watch_face_layout_file[0] != '\0')
        {
            valid = apply_watch_face_layout_file(join_path(pack_dir, watch_face_layout_file),
                                                 record.layouts.watch_face,
                                                 record.has_layouts.watch_face,
                                                 record.watch_face_schema,
                                                 record.has_watch_face_schema) && valid;
        }
        valid = apply_layout_file_if_present(pack_dir,
                                             manifest,
                                             "layout.chat_list",
                                             record.layouts.chat_list,
                                             record.has_layouts.chat_list,
                                             parse_chat_list_variant) && valid;
        const char* chat_conversation_layout_file =
            manifest_value(manifest, "layout.chat_conversation");
        if (chat_conversation_layout_file && chat_conversation_layout_file[0] != '\0')
        {
            valid = apply_chat_conversation_layout_file(
                join_path(pack_dir, chat_conversation_layout_file),
                record.layouts.chat_conversation,
                record.has_layouts.chat_conversation,
                record.chat_conversation_schema,
                record.has_chat_conversation_schema) && valid;
        }
        const char* chat_compose_layout_file =
            manifest_value(manifest, "layout.chat_compose");
        if (chat_compose_layout_file && chat_compose_layout_file[0] != '\0')
        {
            valid = apply_chat_compose_layout_file(join_path(pack_dir, chat_compose_layout_file),
                                                   record.layouts.chat_compose,
                                                   record.has_layouts.chat_compose,
                                                   record.chat_compose_schema,
                                                   record.has_chat_compose_schema) && valid;
        }
        const char* directory_browser_layout_file =
            manifest_value(manifest, "layout.directory_browser");
        if (directory_browser_layout_file && directory_browser_layout_file[0] != '\0')
        {
            valid = apply_directory_browser_layout_file(join_path(pack_dir,
                                                                  directory_browser_layout_file),
                                                        record.layouts.directory_browser,
                                                        record.has_layouts.directory_browser,
                                                        record.directory_browser_schema,
                                                        record.has_directory_browser_schema) && valid;
        }
        const char* map_focus_layout_file = manifest_value(manifest, "layout.map_focus");
        if (map_focus_layout_file && map_focus_layout_file[0] != '\0')
        {
            valid = apply_map_focus_layout_file(join_path(pack_dir, map_focus_layout_file),
                                                record.layouts.map_focus,
                                                record.has_layouts.map_focus,
                                                record.map_focus_schema,
                                                record.has_map_focus_schema) && valid;
        }
        const char* instrument_panel_layout_file = manifest_value(manifest, "layout.instrument_panel");
        if (instrument_panel_layout_file && instrument_panel_layout_file[0] != '\0')
        {
            valid = apply_instrument_panel_layout_file(join_path(pack_dir, instrument_panel_layout_file),
                                                       record.layouts.instrument_panel,
                                                       record.has_layouts.instrument_panel,
                                                       record.instrument_panel_schema,
                                                       record.has_instrument_panel_schema) && valid;
        }
        const char* service_panel_layout_file = manifest_value(manifest, "layout.service_panel");
        if (service_panel_layout_file && service_panel_layout_file[0] != '\0')
        {
            valid = apply_service_panel_layout_file(join_path(pack_dir, service_panel_layout_file),
                                                    record.layouts.service_panel,
                                                    record.has_layouts.service_panel,
                                                    record.service_panel_schema,
                                                    record.has_service_panel_schema) && valid;
        }

        if (!valid)
        {
            continue;
        }

        s_external_presentations.push_back(std::move(record));
    }
}

void ensure_external_presentations_loaded()
{
    if (s_external_presentations_loaded)
    {
        return;
    }

    s_external_presentations_loaded = true;
    s_external_presentations.clear();

#if defined(ARDUINO_ARCH_ESP32)
    load_external_presentations_from_root(kFlashPresentationPackRoot);
#endif
    load_external_presentations_from_root(kPresentationPackRoot);

    std::sort(s_external_presentations.begin(),
              s_external_presentations.end(),
              [](const ExternalPresentationRecord& lhs, const ExternalPresentationRecord& rhs)
              {
                  return lowercase_ascii(lhs.display_name) < lowercase_ascii(rhs.display_name);
              });
}

const PresentationLayoutRecord& builtin_default_layouts()
{
    return kBuiltinPresentations[0].layouts;
}

const char* current_active_presentation_id()
{
    if (s_active_presentation_kind == ActivePresentationKind::External)
    {
        if (const ExternalPresentationRecord* presentation = active_external_presentation())
        {
            return presentation->id.c_str();
        }
    }

    return s_active_builtin_presentation ? s_active_builtin_presentation->info.id
                                         : kBuiltinDefaultPresentationId;
}

const char* canonical_presentation_id_impl(const char* presentation_id)
{
    if (!presentation_id || !*presentation_id)
    {
        return nullptr;
    }

    if (std::strcmp(presentation_id, kBuiltinDefaultPresentationId) == 0 &&
        find_external_presentation(kOfficialDefaultPresentationId) != nullptr)
    {
        return kOfficialDefaultPresentationId;
    }

    if (std::strcmp(presentation_id, kBuiltinDirectoryStackedPresentationId) == 0 &&
        find_external_presentation(kOfficialDirectoryStackedPresentationId) != nullptr)
    {
        return kOfficialDirectoryStackedPresentationId;
    }

    return presentation_id;
}

bool should_expose_presentation_publicly(const PresentationInfo& info)
{
    if (!info.id)
    {
        return false;
    }

    if (std::strcmp(info.id, kBuiltinDefaultPresentationId) == 0 &&
        find_external_presentation(kOfficialDefaultPresentationId) != nullptr)
    {
        return false;
    }

    if (std::strcmp(info.id, kBuiltinDirectoryStackedPresentationId) == 0 &&
        find_external_presentation(kOfficialDirectoryStackedPresentationId) != nullptr)
    {
        return false;
    }

    return true;
}

int public_presentation_rank(const PresentationInfo& info)
{
    if (info.id && std::strcmp(info.id, kOfficialDefaultPresentationId) == 0)
    {
        return 0;
    }
    if (info.id && std::strcmp(info.id, kOfficialDirectoryStackedPresentationId) == 0)
    {
        return 1;
    }
    if (!info.builtin)
    {
        return 2;
    }
    return 3;
}

void collect_public_presentation_infos(std::vector<PresentationInfo>& out_infos)
{
    out_infos.clear();
    out_infos.reserve(builtin_presentation_count() + s_external_presentations.size());

    for (const ExternalPresentationRecord& record : s_external_presentations)
    {
        PresentationInfo info = make_external_presentation_info(record);
        if (should_expose_presentation_publicly(info))
        {
            out_infos.push_back(info);
        }
    }

    for (std::size_t index = 0; index < builtin_presentation_count(); ++index)
    {
        PresentationInfo info{};
        if (builtin_presentation_at(index, info) && should_expose_presentation_publicly(info))
        {
            out_infos.push_back(info);
        }
    }

    std::sort(out_infos.begin(),
              out_infos.end(),
              [](const PresentationInfo& lhs, const PresentationInfo& rhs)
              {
                  const int lhs_rank = public_presentation_rank(lhs);
                  const int rhs_rank = public_presentation_rank(rhs);
                  if (lhs_rank != rhs_rank)
                  {
                      return lhs_rank < rhs_rank;
                  }

                  const char* lhs_name = lhs.display_name ? lhs.display_name : "";
                  const char* rhs_name = rhs.display_name ? rhs.display_name : "";
                  return lowercase_ascii(lhs_name) < lowercase_ascii(rhs_name);
              });
}

MenuDashboardLayout resolve_menu_dashboard_layout_for_presentation_id(const char* presentation_id,
                                                                      std::size_t depth)
{
    if (depth > 8)
    {
        return builtin_default_layouts().menu_dashboard;
    }

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (presentation->has_layouts.menu_dashboard)
        {
            return presentation->layouts.menu_dashboard;
        }
        if (!presentation->base_presentation_id.empty())
        {
            return resolve_menu_dashboard_layout_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U);
        }
        return builtin_default_layouts().menu_dashboard;
    }

    if (const BuiltinPresentationRecord* presentation = find_builtin_presentation(presentation_id))
    {
        return presentation->layouts.menu_dashboard;
    }

    return builtin_default_layouts().menu_dashboard;
}

BootSplashLayout resolve_boot_splash_layout_for_presentation_id(const char* presentation_id,
                                                                std::size_t depth)
{
    if (depth > 8)
    {
        return builtin_default_layouts().boot_splash;
    }

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (presentation->has_layouts.boot_splash)
        {
            return presentation->layouts.boot_splash;
        }
        if (!presentation->base_presentation_id.empty())
        {
            return resolve_boot_splash_layout_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U);
        }
        return builtin_default_layouts().boot_splash;
    }

    if (const BuiltinPresentationRecord* presentation = find_builtin_presentation(presentation_id))
    {
        return presentation->layouts.boot_splash;
    }

    return builtin_default_layouts().boot_splash;
}

WatchFaceLayout resolve_watch_face_layout_for_presentation_id(const char* presentation_id,
                                                              std::size_t depth)
{
    if (depth > 8)
    {
        return builtin_default_layouts().watch_face;
    }

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (presentation->has_layouts.watch_face)
        {
            return presentation->layouts.watch_face;
        }
        if (!presentation->base_presentation_id.empty())
        {
            return resolve_watch_face_layout_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U);
        }
        return builtin_default_layouts().watch_face;
    }

    if (const BuiltinPresentationRecord* presentation = find_builtin_presentation(presentation_id))
    {
        return presentation->layouts.watch_face;
    }

    return builtin_default_layouts().watch_face;
}

bool resolve_menu_dashboard_schema_for_presentation_id(const char* presentation_id,
                                                       std::size_t depth,
                                                       MenuDashboardSchema& out_schema)
{
    if (depth > 8)
    {
        return false;
    }

    bool resolved = false;

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (!presentation->base_presentation_id.empty())
        {
            resolved = resolve_menu_dashboard_schema_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U, out_schema);
        }

        if (presentation->has_menu_dashboard_schema)
        {
            merge_menu_dashboard_schema(out_schema, presentation->menu_dashboard_schema);
            resolved = true;
        }

        return resolved;
    }

    return false;
}

bool resolve_boot_splash_schema_for_presentation_id(const char* presentation_id,
                                                    std::size_t depth,
                                                    BootSplashSchema& out_schema)
{
    if (depth > 8)
    {
        return false;
    }

    bool resolved = false;

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (!presentation->base_presentation_id.empty())
        {
            resolved = resolve_boot_splash_schema_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U, out_schema);
        }

        if (presentation->has_boot_splash_schema)
        {
            merge_boot_splash_schema(out_schema, presentation->boot_splash_schema);
            resolved = true;
        }

        return resolved;
    }

    return false;
}

bool resolve_watch_face_schema_for_presentation_id(const char* presentation_id,
                                                   std::size_t depth,
                                                   WatchFaceSchema& out_schema)
{
    if (depth > 8)
    {
        return false;
    }

    bool resolved = false;

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (!presentation->base_presentation_id.empty())
        {
            resolved = resolve_watch_face_schema_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U, out_schema);
        }

        if (presentation->has_watch_face_schema)
        {
            merge_watch_face_schema(out_schema, presentation->watch_face_schema);
            resolved = true;
        }

        return resolved;
    }

    return false;
}

ChatListLayout resolve_chat_list_layout_for_presentation_id(const char* presentation_id,
                                                            std::size_t depth)
{
    if (depth > 8)
    {
        return builtin_default_layouts().chat_list;
    }

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (presentation->has_layouts.chat_list)
        {
            return presentation->layouts.chat_list;
        }
        if (!presentation->base_presentation_id.empty())
        {
            return resolve_chat_list_layout_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U);
        }
        return builtin_default_layouts().chat_list;
    }

    if (const BuiltinPresentationRecord* presentation = find_builtin_presentation(presentation_id))
    {
        return presentation->layouts.chat_list;
    }

    return builtin_default_layouts().chat_list;
}

DirectoryBrowserLayout resolve_directory_browser_layout_for_presentation_id(const char* presentation_id,
                                                                           std::size_t depth)
{
    if (depth > 8)
    {
        return builtin_default_layouts().directory_browser;
    }

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (presentation->has_layouts.directory_browser)
        {
            return presentation->layouts.directory_browser;
        }
        if (!presentation->base_presentation_id.empty())
        {
            return resolve_directory_browser_layout_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U);
        }
        return builtin_default_layouts().directory_browser;
    }

    if (const BuiltinPresentationRecord* presentation = find_builtin_presentation(presentation_id))
    {
        return presentation->layouts.directory_browser;
    }

    return builtin_default_layouts().directory_browser;
}

bool resolve_directory_browser_schema_for_presentation_id(const char* presentation_id,
                                                          std::size_t depth,
                                                          DirectoryBrowserSchema& out_schema)
{
    if (depth > 8)
    {
        return false;
    }

    bool resolved = false;

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (!presentation->base_presentation_id.empty())
        {
            resolved = resolve_directory_browser_schema_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U, out_schema);
        }

        if (presentation->has_directory_browser_schema)
        {
            merge_directory_browser_schema(out_schema, presentation->directory_browser_schema);
            resolved = true;
        }

        return resolved;
    }

    return false;
}

bool resolve_chat_conversation_schema_for_presentation_id(const char* presentation_id,
                                                          std::size_t depth,
                                                          ChatConversationSchema& out_schema)
{
    if (depth > 8)
    {
        return false;
    }

    bool resolved = false;

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (!presentation->base_presentation_id.empty())
        {
            resolved = resolve_chat_conversation_schema_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U, out_schema);
        }

        if (presentation->has_chat_conversation_schema)
        {
            merge_chat_conversation_schema(out_schema, presentation->chat_conversation_schema);
            resolved = true;
        }

        return resolved;
    }

    return false;
}

bool resolve_chat_compose_schema_for_presentation_id(const char* presentation_id,
                                                     std::size_t depth,
                                                     ChatComposeSchema& out_schema)
{
    if (depth > 8)
    {
        return false;
    }

    bool resolved = false;

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (!presentation->base_presentation_id.empty())
        {
            resolved = resolve_chat_compose_schema_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U, out_schema);
        }

        if (presentation->has_chat_compose_schema)
        {
            merge_chat_compose_schema(out_schema, presentation->chat_compose_schema);
            resolved = true;
        }

        return resolved;
    }

    return false;
}

bool resolve_map_focus_schema_for_presentation_id(const char* presentation_id,
                                                  std::size_t depth,
                                                  MapFocusSchema& out_schema)
{
    if (depth > 8)
    {
        return false;
    }

    bool resolved = false;

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (!presentation->base_presentation_id.empty())
        {
            resolved = resolve_map_focus_schema_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U, out_schema);
        }

        if (presentation->has_map_focus_schema)
        {
            merge_map_focus_schema(out_schema, presentation->map_focus_schema);
            resolved = true;
        }

        return resolved;
    }

    return false;
}

bool resolve_instrument_panel_schema_for_presentation_id(const char* presentation_id,
                                                         std::size_t depth,
                                                         InstrumentPanelSchema& out_schema)
{
    if (depth > 8)
    {
        return false;
    }

    bool resolved = false;

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (!presentation->base_presentation_id.empty())
        {
            resolved = resolve_instrument_panel_schema_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U, out_schema);
        }

        if (presentation->has_instrument_panel_schema)
        {
            merge_instrument_panel_schema(out_schema, presentation->instrument_panel_schema);
            resolved = true;
        }

        return resolved;
    }

    return false;
}

bool resolve_service_panel_schema_for_presentation_id(const char* presentation_id,
                                                      std::size_t depth,
                                                      ServicePanelSchema& out_schema)
{
    if (depth > 8)
    {
        return false;
    }

    bool resolved = false;

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (!presentation->base_presentation_id.empty())
        {
            resolved = resolve_service_panel_schema_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U, out_schema);
        }

        if (presentation->has_service_panel_schema)
        {
            merge_service_panel_schema(out_schema, presentation->service_panel_schema);
            resolved = true;
        }

        return resolved;
    }

    return false;
}

ChatConversationLayout resolve_chat_conversation_layout_for_presentation_id(const char* presentation_id,
                                                                            std::size_t depth)
{
    if (depth > 8)
    {
        return builtin_default_layouts().chat_conversation;
    }

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (presentation->has_layouts.chat_conversation)
        {
            return presentation->layouts.chat_conversation;
        }
        if (!presentation->base_presentation_id.empty())
        {
            return resolve_chat_conversation_layout_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U);
        }
        return builtin_default_layouts().chat_conversation;
    }

    if (const BuiltinPresentationRecord* presentation = find_builtin_presentation(presentation_id))
    {
        return presentation->layouts.chat_conversation;
    }

    return builtin_default_layouts().chat_conversation;
}

ChatComposeLayout resolve_chat_compose_layout_for_presentation_id(const char* presentation_id,
                                                                  std::size_t depth)
{
    if (depth > 8)
    {
        return builtin_default_layouts().chat_compose;
    }

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (presentation->has_layouts.chat_compose)
        {
            return presentation->layouts.chat_compose;
        }
        if (!presentation->base_presentation_id.empty())
        {
            return resolve_chat_compose_layout_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U);
        }
        return builtin_default_layouts().chat_compose;
    }

    if (const BuiltinPresentationRecord* presentation = find_builtin_presentation(presentation_id))
    {
        return presentation->layouts.chat_compose;
    }

    return builtin_default_layouts().chat_compose;
}

MapFocusLayout resolve_map_focus_layout_for_presentation_id(const char* presentation_id,
                                                            std::size_t depth)
{
    if (depth > 8)
    {
        return builtin_default_layouts().map_focus;
    }

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (presentation->has_layouts.map_focus)
        {
            return presentation->layouts.map_focus;
        }
        if (!presentation->base_presentation_id.empty())
        {
            return resolve_map_focus_layout_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U);
        }
        return builtin_default_layouts().map_focus;
    }

    if (const BuiltinPresentationRecord* presentation = find_builtin_presentation(presentation_id))
    {
        return presentation->layouts.map_focus;
    }

    return builtin_default_layouts().map_focus;
}

InstrumentPanelLayout resolve_instrument_panel_layout_for_presentation_id(const char* presentation_id,
                                                                          std::size_t depth)
{
    if (depth > 8)
    {
        return builtin_default_layouts().instrument_panel;
    }

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (presentation->has_layouts.instrument_panel)
        {
            return presentation->layouts.instrument_panel;
        }
        if (!presentation->base_presentation_id.empty())
        {
            return resolve_instrument_panel_layout_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U);
        }
        return builtin_default_layouts().instrument_panel;
    }

    if (const BuiltinPresentationRecord* presentation = find_builtin_presentation(presentation_id))
    {
        return presentation->layouts.instrument_panel;
    }

    return builtin_default_layouts().instrument_panel;
}

ServicePanelLayout resolve_service_panel_layout_for_presentation_id(const char* presentation_id,
                                                                    std::size_t depth)
{
    if (depth > 8)
    {
        return builtin_default_layouts().service_panel;
    }

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        if (presentation->has_layouts.service_panel)
        {
            return presentation->layouts.service_panel;
        }
        if (!presentation->base_presentation_id.empty())
        {
            return resolve_service_panel_layout_for_presentation_id(
                presentation->base_presentation_id.c_str(), depth + 1U);
        }
        return builtin_default_layouts().service_panel;
    }

    if (const BuiltinPresentationRecord* presentation = find_builtin_presentation(presentation_id))
    {
        return presentation->layouts.service_panel;
    }

    return builtin_default_layouts().service_panel;
}

} // namespace

void reset_active_presentation()
{
    ensure_external_presentations_loaded();
    (void)set_active_presentation(preferred_default_presentation_id());
}

bool set_active_presentation(const char* presentation_id)
{
    ensure_external_presentations_loaded();
    presentation_id = canonical_presentation_id_impl(presentation_id);

    if (const BuiltinPresentationRecord* presentation = find_builtin_presentation(presentation_id))
    {
        if (s_active_presentation_kind != ActivePresentationKind::Builtin ||
            s_active_builtin_presentation != presentation)
        {
            s_active_presentation_kind = ActivePresentationKind::Builtin;
            s_active_builtin_presentation = presentation;
            s_active_external_presentation_index = kInvalidIndex;
            ++s_presentation_revision;
        }
        return true;
    }

    const std::size_t external_index = find_external_presentation_index(presentation_id);
    if (external_index != kInvalidIndex)
    {
        if (s_active_presentation_kind != ActivePresentationKind::External ||
            s_active_external_presentation_index != external_index)
        {
            s_active_presentation_kind = ActivePresentationKind::External;
            s_active_builtin_presentation = nullptr;
            s_active_external_presentation_index = external_index;
            ++s_presentation_revision;
        }
        return true;
    }

    return false;
}

const char* active_presentation_id()
{
    ensure_external_presentations_loaded();
    return current_active_presentation_id();
}

std::uint32_t active_presentation_revision()
{
    return s_presentation_revision;
}

void reload_installed_presentations()
{
    const std::string previous_active_id = active_presentation_id();

    s_external_presentations.clear();
    s_external_presentations_loaded = false;
    ensure_external_presentations_loaded();

    if (!previous_active_id.empty() && set_active_presentation(previous_active_id.c_str()))
    {
        return;
    }

    reset_active_presentation();
}

std::size_t builtin_presentation_count()
{
    return sizeof(kBuiltinPresentations) / sizeof(kBuiltinPresentations[0]);
}

bool builtin_presentation_at(std::size_t index, PresentationInfo& out_info)
{
    if (index >= builtin_presentation_count())
    {
        out_info = PresentationInfo{};
        return false;
    }

    out_info = kBuiltinPresentations[index].info;
    return true;
}

std::size_t presentation_count()
{
    ensure_external_presentations_loaded();
    std::vector<PresentationInfo> presentations;
    collect_public_presentation_infos(presentations);
    return presentations.size();
}

bool presentation_at(std::size_t index, PresentationInfo& out_info)
{
    ensure_external_presentations_loaded();
    std::vector<PresentationInfo> presentations;
    collect_public_presentation_infos(presentations);
    if (index >= presentations.size())
    {
        out_info = PresentationInfo{};
        return false;
    }

    out_info = presentations[index];
    return true;
}

bool describe_presentation(const char* presentation_id, PresentationInfo& out_info)
{
    ensure_external_presentations_loaded();
    presentation_id = canonical_presentation_id_impl(presentation_id);

    if (const BuiltinPresentationRecord* presentation = find_builtin_presentation(presentation_id))
    {
        out_info = presentation->info;
        return true;
    }

    if (const ExternalPresentationRecord* presentation = find_external_presentation(presentation_id))
    {
        out_info = make_external_presentation_info(*presentation);
        return true;
    }

    out_info = PresentationInfo{};
    return false;
}

const char* preferred_default_presentation_id()
{
    ensure_external_presentations_loaded();
    return find_external_presentation(kOfficialDefaultPresentationId) != nullptr
               ? kOfficialDefaultPresentationId
               : kBuiltinDefaultPresentationId;
}

MenuDashboardLayout active_menu_dashboard_layout()
{
    ensure_external_presentations_loaded();
    return resolve_menu_dashboard_layout_for_presentation_id(active_presentation_id(), 0);
}

bool resolve_active_menu_dashboard_schema(MenuDashboardSchema& out_schema)
{
    ensure_external_presentations_loaded();
    out_schema = MenuDashboardSchema{};
    return resolve_menu_dashboard_schema_for_presentation_id(active_presentation_id(), 0,
                                                             out_schema);
}

BootSplashLayout active_boot_splash_layout()
{
    ensure_external_presentations_loaded();
    return resolve_boot_splash_layout_for_presentation_id(active_presentation_id(), 0);
}

bool resolve_active_boot_splash_schema(BootSplashSchema& out_schema)
{
    ensure_external_presentations_loaded();
    out_schema = BootSplashSchema{};
    return resolve_boot_splash_schema_for_presentation_id(active_presentation_id(), 0,
                                                          out_schema);
}

WatchFaceLayout active_watch_face_layout()
{
    ensure_external_presentations_loaded();
    return resolve_watch_face_layout_for_presentation_id(active_presentation_id(), 0);
}

bool resolve_active_watch_face_schema(WatchFaceSchema& out_schema)
{
    ensure_external_presentations_loaded();
    out_schema = WatchFaceSchema{};
    return resolve_watch_face_schema_for_presentation_id(active_presentation_id(), 0, out_schema);
}

ChatListLayout active_chat_list_layout()
{
    ensure_external_presentations_loaded();
    return resolve_chat_list_layout_for_presentation_id(active_presentation_id(), 0);
}

DirectoryBrowserLayout active_directory_browser_layout()
{
    ensure_external_presentations_loaded();
    return resolve_directory_browser_layout_for_presentation_id(active_presentation_id(), 0);
}

bool resolve_active_directory_browser_schema(DirectoryBrowserSchema& out_schema)
{
    ensure_external_presentations_loaded();
    out_schema = DirectoryBrowserSchema{};
    return resolve_directory_browser_schema_for_presentation_id(active_presentation_id(), 0,
                                                                out_schema);
}

bool directory_browser_uses_stacked_selectors()
{
    return active_directory_browser_layout() == DirectoryBrowserLayout::StackedSelectors;
}

ChatConversationLayout active_chat_conversation_layout()
{
    ensure_external_presentations_loaded();
    return resolve_chat_conversation_layout_for_presentation_id(active_presentation_id(), 0);
}

bool resolve_active_chat_conversation_schema(ChatConversationSchema& out_schema)
{
    ensure_external_presentations_loaded();
    out_schema = ChatConversationSchema{};
    return resolve_chat_conversation_schema_for_presentation_id(active_presentation_id(), 0,
                                                                out_schema);
}

ChatComposeLayout active_chat_compose_layout()
{
    ensure_external_presentations_loaded();
    return resolve_chat_compose_layout_for_presentation_id(active_presentation_id(), 0);
}

bool resolve_active_chat_compose_schema(ChatComposeSchema& out_schema)
{
    ensure_external_presentations_loaded();
    out_schema = ChatComposeSchema{};
    return resolve_chat_compose_schema_for_presentation_id(active_presentation_id(), 0,
                                                           out_schema);
}

MapFocusLayout active_map_focus_layout()
{
    ensure_external_presentations_loaded();
    return resolve_map_focus_layout_for_presentation_id(active_presentation_id(), 0);
}

bool resolve_active_map_focus_schema(MapFocusSchema& out_schema)
{
    ensure_external_presentations_loaded();
    out_schema = MapFocusSchema{};
    return resolve_map_focus_schema_for_presentation_id(active_presentation_id(), 0, out_schema);
}

InstrumentPanelLayout active_instrument_panel_layout()
{
    ensure_external_presentations_loaded();
    return resolve_instrument_panel_layout_for_presentation_id(active_presentation_id(), 0);
}

bool resolve_active_instrument_panel_schema(InstrumentPanelSchema& out_schema)
{
    ensure_external_presentations_loaded();
    out_schema = InstrumentPanelSchema{};
    return resolve_instrument_panel_schema_for_presentation_id(active_presentation_id(), 0,
                                                               out_schema);
}

ServicePanelLayout active_service_panel_layout()
{
    ensure_external_presentations_loaded();
    return resolve_service_panel_layout_for_presentation_id(active_presentation_id(), 0);
}

bool resolve_active_service_panel_schema(ServicePanelSchema& out_schema)
{
    ensure_external_presentations_loaded();
    out_schema = ServicePanelSchema{};
    return resolve_service_panel_schema_for_presentation_id(active_presentation_id(), 0,
                                                            out_schema);
}

} // namespace ui::presentation
