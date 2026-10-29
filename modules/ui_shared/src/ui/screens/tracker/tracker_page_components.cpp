#include "ui/screens/tracker/tracker_page_components.h"
#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "gps/kml/image_sources.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/navigation_runtime.h"
#include "platform/ui/network_runtime.h"
#include "platform/ui/route_storage.h"
#include "platform/ui/tracker_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "track_sharing/installed_package_path.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/components/bottom_bar.h"
#include "ui/components/dialog_frame.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/screens/tracker/tracker_item_card_view.h"
#include "ui/screens/tracker/tracker_navigation_dialog.h"
#include "ui/screens/tracker/tracker_page_input.h"
#include "ui/screens/tracker/tracker_page_layout.h"
#include "ui/screens/tracker/tracker_state.h"
#include "ui/support/card_file_location.h"
#include "ui/support/card_location.h"
#include "ui/support/disconnected_route_line.h"
#include "ui/support/kml_route_projection.h"
#include "ui/support/lvgl_fs_utils.h"
#include "ui/support/route_image_kml_reader.h"
#include "ui/support/route_image_memory.h"
#include "ui/support/route_image_source_state.h"
#include "ui/support/route_preview_viewport.h"
#include "ui/support/route_segment_sampling.h"
#include "ui/support/tmroute_map.h"
#include "ui/support/tmroute_preview.h"
#include "ui/ui_common.h"
#include "ui/widgets/map/map_viewport.h"
#include "ui/widgets/route_elevation_profile.h"
#include "ui/widgets/route_image_operation_presenter.h"
#include "ui/widgets/route_image_strip.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif

#if !defined(LV_FONT_MONTSERRAT_10) || !LV_FONT_MONTSERRAT_10
#define lv_font_montserrat_10 lv_font_montserrat_14
#endif

namespace tracker
{
namespace ui
{
namespace components
{

namespace
{
constexpr int kBottomBarButtonWidth = 70;
constexpr uintptr_t kListUserDataOffset = 1;
static constexpr intptr_t kBackListItemUserData = -2;
std::vector<std::string> s_route_names;
std::vector<std::string> s_record_names;
std::string s_record_empty_text = "No tracks yet";
std::string s_route_empty_text = "No KML routes";
lv_timer_t* s_record_list_refresh_timer = nullptr;

constexpr std::size_t kPreviewMaxDrawPoints = 96;
constexpr std::size_t kPreviewMaxImages = 64;
constexpr lv_coord_t kPreviewButtonGap = 3;
constexpr lv_coord_t kPreviewButtonWidth = 78;
constexpr lv_coord_t kPreviewButtonWidthDense = 66;
constexpr uint32_t kPreviewDownloadPollMs = 350;

struct RoutePreviewPoint
{
    double lat = 0.0;
    double lon = 0.0;
    double altitude_m = 0.0;
    bool has_altitude = false;
    bool break_before = true;
};

static_assert(sizeof(RoutePreviewPoint) <= 32, "Segment flag must fit existing point padding");

struct RoutePreviewImage
{
    uint8_t source_hash[32]{};
    bool source_hash_valid = false;
    std::string url{};
    std::string local_path{};
    std::string preview_path{};
    std::string view_path{};
    double lat = 0.0;
    double lon = 0.0;
    bool has_position = false;
    bool downloaded = false;
    bool preview_ready = false;
    bool view_ready = false;
};

struct RoutePreviewMetrics
{
    double min_altitude_m = 0.0;
    double max_altitude_m = 0.0;
    double ascent_m = 0.0;
    double descent_m = 0.0;
    double distance_m = 0.0;
    std::size_t altitude_count = 0;
};

enum class RoutePreviewDownloadState : uint8_t
{
    Idle = 0,
    Downloading,
    Done,
    Failed,
};

::ui::support::RouteImageVector<RoutePreviewPoint, 96> s_preview_route_points;
::ui::support::RouteImageVector<RoutePreviewImage> s_preview_images;
::ui::support::RouteImageVector<lv_point_precise_t, 192> s_preview_map_line_points;
::ui::support::RouteImageVector<::ui::widgets::route_elevation_profile::Sample, 96> s_preview_elevation_samples;
::ui::widgets::route_elevation_profile::Widget s_preview_elevation_profile;
::ui::widgets::route_image_strip::Widget s_preview_image_strip;
std::vector<::ui::widgets::route_image_strip::Item> s_preview_image_strip_items;
uint32_t s_preview_image_strip_items_hash = 0;
RoutePreviewMetrics s_preview_metrics;
std::string s_preview_route_name;
std::string s_preview_asset_id;
std::string s_preview_status_text;
RoutePreviewDownloadState s_preview_download_state = RoutePreviewDownloadState::Idle;
int s_preview_selected_image = 0;
bool s_preview_elevation_visible = false;
bool s_preview_image_strip_visible = false;
bool s_preview_map_loader_paused = false;
bool s_preview_image_saved_state_known = false;
bool s_preview_image_cache_state_known = false;
std::size_t s_preview_saved_image_count_hint = 0;
::ui::widgets::map::Runtime s_preview_map_runtime;
::ui::support::RoutePreviewViewport s_preview_view;
::ui::support::TmRouteMap* s_preview_native_route = nullptr;
lv_timer_t* s_preview_native_timer = nullptr;
lv_obj_t* s_preview_map_controls = nullptr;
lv_timer_t* s_preview_download_poll_timer = nullptr;
bool s_preview_download_refresh_map_on_finish = false;

constexpr uint32_t kPanelBtnBg = 0xFAF0D8;
constexpr uint32_t kPanelBtnBorder = 0xE7C98F;
constexpr uint32_t kPanelBtnFocused = 0xEBA341;
constexpr uint32_t kPanelBtnText = 0x6B4A1E;
constexpr uint32_t kPanelTextMuted = 0x8A6A3A;

bool s_btn_styles_inited = false;
lv_style_t s_btn_main;
lv_style_t s_btn_focused;
lv_style_t s_btn_disabled;
lv_style_t s_btn_label;

enum class ActionMenuCommand : uintptr_t
{
    Load = 1,
    Unload,
    Preview,
    Delete,
    Cancel,
};

lv_group_t* tracker_group();
const ::ui::page_profile::PageLayoutProfile& page_profile();
lv_coord_t filter_panel_width();
lv_coord_t filter_button_height();
lv_coord_t list_item_height();
lv_coord_t modal_button_width();
lv_coord_t action_menu_button_height();
lv_coord_t action_menu_row_gap();
void update_record_status();
void refresh_route_list();
void update_start_stop_button();
void update_record_page();
void update_route_status();
void update_route_page();
void cancel_deferred_record_list_refresh();
void schedule_deferred_record_list_refresh(uint32_t delay_ms);
bool can_delete_selected_item();
bool can_load_selected_route();
bool can_unload_active_route();
bool can_preview_selected_route();
bool is_any_modal_open();
void on_route_load_clicked(lv_event_t* e);
void on_route_unload_clicked(lv_event_t* e);
void open_route_preview_page();
void close_route_preview_page();
void render_route_preview_page();
void refresh_route_preview_map();
void create_preview_map_controls();
void update_route_preview_status();
void show_route_preview_notice(const char* message, uint32_t duration_ms);
void update_route_preview_load_button();
void update_route_preview_buttons();
bool preview_selected_route_is_active();
void sync_route_preview_download_status();
void sync_route_preview_map_loader_pause(
    const platform::ui::route_storage::RouteImageDownloadStatus& status);
void sync_route_preview_map_loader_pause();
void ensure_route_preview_download_poll_timer();
void ensure_route_preview_image_cache_build(bool refresh_state = true);
void sync_route_preview_elevation_visibility();
void toggle_route_preview_elevation();
void cycle_route_preview_map_layer();
void sync_route_preview_image_strip();
void toggle_route_preview_image_strip();
void open_route_preview_help_modal();
void close_route_preview_help_modal();
void on_route_preview_key(lv_event_t* e);
void on_route_preview_download_clicked(lv_event_t* e);
void on_route_preview_load_clicked(lv_event_t* e);
void open_delete_confirm_modal();
void on_list_item_clicked(lv_event_t* e);
void on_list_item_focused(lv_event_t* e);
void on_list_item_defocused(lv_event_t* e);
void on_backspace_key(lv_event_t* e);
lv_obj_t* create_action_menu_button(lv_obj_t* parent, const char* text);
void on_action_menu_key(lv_event_t* e);
void on_action_menu_item_clicked(lv_event_t* e);
void open_action_menu_modal();
void focus_main_panel();
std::string format_list_name(const std::string& name);
void clear_list_items();
lv_obj_t* create_list_item_button(const std::string& text, intptr_t user_data, bool checked, bool disabled);

void on_back(void*)
{
    if (navigation_dialog::is_open())
    {
        navigation_dialog::close();
        return;
    }
    if (g_tracker_state.route_preview_help_modal)
    {
        close_route_preview_help_modal();
        return;
    }
    if (g_tracker_state.route_preview_page)
    {
        close_route_preview_page();
        return;
    }
    ui_request_exit_to_menu();
}

const ::ui::page_profile::PageLayoutProfile& page_profile()
{
    return ::ui::page_profile::current();
}

lv_coord_t filter_panel_width()
{
    return page_profile().filter_panel_width;
}

lv_coord_t filter_button_height()
{
    return page_profile().filter_button_height;
}

lv_coord_t list_item_height()
{
    return page_profile().list_item_height;
}

lv_coord_t modal_button_width()
{
    return ::ui::page_profile::resolve_control_button_min_width();
}

lv_coord_t action_menu_button_height()
{
    return page_profile().filter_button_height;
}

lv_coord_t action_menu_row_gap()
{
    if (page_profile().dense)
    {
        return 2;
    }
    return page_profile().large_touch_hitbox ? 6 : 4;
}

void on_backspace_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_BACKSPACE && key != LV_KEY_ESC)
    {
        return;
    }
    if (g_tracker_state.top_bar.back_btn)
    {
        lv_obj_send_event(g_tracker_state.top_bar.back_btn, LV_EVENT_CLICKED, nullptr);
        return;
    }
    on_back(nullptr);
}

void modal_prepare_group()
{
    auto& state = g_tracker_state;
    if (!state.modal_group)
    {
        state.modal_group = lv_group_create();
    }
    lv_group_remove_all_objs(state.modal_group);
    state.prev_group = lv_group_get_default();
    lv_group_t* group = tracker_group();
    if (group && state.prev_group != group)
    {
        state.prev_group = group;
    }
    set_default_group(state.modal_group);
}

void modal_restore_group()
{
    auto& state = g_tracker_state;
    lv_group_t* restore = state.prev_group ? state.prev_group : tracker_group();
    if (restore)
    {
        set_default_group(restore);
    }
    state.prev_group = nullptr;
}

void modal_close(lv_obj_t*& modal_obj)
{
    if (modal_obj)
    {
        lv_obj_del(modal_obj);
        modal_obj = nullptr;
    }
    modal_restore_group();
}

bool is_any_modal_open()
{
    return navigation_dialog::is_open() || g_tracker_state.del_confirm_modal != nullptr ||
           g_tracker_state.action_menu_modal != nullptr ||
           g_tracker_state.route_preview_help_modal != nullptr;
}

void bind_route_preview_key_handler(lv_obj_t* obj)
{
    if (!obj || !lv_obj_is_valid(obj))
    {
        return;
    }
    lv_obj_remove_event_cb(obj, on_route_preview_key);
    lv_obj_add_event_cb(obj, on_route_preview_key, LV_EVENT_KEY, nullptr);
}

void unbind_route_preview_key_handler(lv_obj_t* obj)
{
    if (!obj || !lv_obj_is_valid(obj))
    {
        return;
    }
    lv_obj_remove_event_cb(obj, on_route_preview_key);
}

void init_button_styles()
{
    if (s_btn_styles_inited)
    {
        return;
    }
    lv_style_init(&s_btn_main);
    lv_style_set_bg_color(&s_btn_main, lv_color_hex(kPanelBtnBg));
    lv_style_set_bg_opa(&s_btn_main, LV_OPA_COVER);
    lv_style_set_border_width(&s_btn_main, 1);
    lv_style_set_border_color(&s_btn_main, lv_color_hex(kPanelBtnBorder));
    lv_style_set_radius(&s_btn_main, 6);
    lv_style_set_text_color(&s_btn_main, lv_color_hex(kPanelBtnText));

    lv_style_init(&s_btn_focused);
    lv_style_set_bg_color(&s_btn_focused, lv_color_hex(kPanelBtnFocused));
    lv_style_set_bg_opa(&s_btn_focused, LV_OPA_COVER);
    lv_style_set_outline_width(&s_btn_focused, 0);

    lv_style_init(&s_btn_disabled);
    lv_style_set_bg_opa(&s_btn_disabled, LV_OPA_50);

    lv_style_init(&s_btn_label);
    lv_style_set_text_color(&s_btn_label, lv_color_hex(kPanelBtnText));
    lv_style_set_text_font(&s_btn_label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()));

    s_btn_styles_inited = true;
}

void apply_action_button(lv_obj_t* btn, lv_obj_t* label)
{
    if (!btn)
    {
        return;
    }
    init_button_styles();
    lv_obj_add_style(btn, &s_btn_main, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_btn_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_style(btn, &s_btn_disabled, LV_PART_MAIN | LV_STATE_DISABLED);
    if (label)
    {
        lv_obj_add_style(label, &s_btn_label, LV_PART_MAIN);
    }
}

void apply_list_button(lv_obj_t* btn)
{
    if (!btn)
    {
        return;
    }
    init_button_styles();
    lv_obj_add_style(btn, &s_btn_main, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_btn_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_style(btn, &s_btn_focused, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_add_style(btn, &s_btn_disabled, LV_PART_MAIN | LV_STATE_DISABLED);
    if (lv_obj_t* label = lv_obj_get_child(btn, -1))
    {
        lv_obj_add_style(label, &s_btn_label, LV_PART_MAIN);
    }
}

void style_mode_button(lv_obj_t* btn, lv_obj_t* label, bool active)
{
    if (!btn)
    {
        return;
    }
    ::ui::components::bottom_bar::style_item(btn);
    if (active) lv_obj_add_state(btn, LV_STATE_CHECKED);
    else lv_obj_clear_state(btn, LV_STATE_CHECKED);
    if (label)
    {
        lv_obj_set_style_text_color(label, lv_color_hex(0x6B4A1E), LV_PART_MAIN);
    }
}

void update_mode_buttons()
{
    auto& state = g_tracker_state;
    bool record_active = (state.mode == TrackerPageState::Mode::Record);
    style_mode_button(state.mode_record_btn, state.mode_record_label, record_active);
    style_mode_button(state.mode_route_btn, state.mode_route_label, state.mode == TrackerPageState::Mode::Route);
    style_mode_button(state.mode_navigation_btn, state.mode_navigation_label, state.mode == TrackerPageState::Mode::Navigation);
}

std::string path_basename(const std::string& path)
{
    const char* base = strrchr(path.c_str(), '/');
    if (base && base[1] != '\0')
    {
        return std::string(base + 1);
    }
    return path;
}

bool route_path_looks_degraded(const std::string& path)
{
    return path.find('?') != std::string::npos;
}

bool can_delete_selected_item()
{
    auto& state = g_tracker_state;
    bool can_delete = false;
    if (state.mode == TrackerPageState::Mode::Record)
    {
        if (state.selected_record_idx >= 0 &&
            state.selected_record_idx < static_cast<int>(s_record_names.size()))
        {
            can_delete = true;
            if (platform::ui::tracker::is_recording())
            {
                std::string current;
                platform::ui::tracker::current_path(current);
                std::string base = path_basename(current);
                if (base == s_record_names[state.selected_record_idx])
                {
                    can_delete = false;
                }
            }
        }
    }
    else
    {
        if (state.selected_route_idx >= 0 &&
            state.selected_route_idx < static_cast<int>(s_route_names.size()))
        {
            can_delete = true;
        }
    }
    return can_delete;
}

bool can_load_selected_route()
{
    auto& state = g_tracker_state;
    return state.mode != TrackerPageState::Mode::Record &&
           state.active_route.empty() &&
           state.selected_route_idx >= 0 &&
           !state.selected_route.empty();
}

bool can_unload_active_route()
{
    auto& state = g_tracker_state;
    return state.mode != TrackerPageState::Mode::Record &&
           !state.active_route.empty();
}

bool can_preview_selected_route()
{
    auto& state = g_tracker_state;
    return state.mode != TrackerPageState::Mode::Record &&
           state.selected_route_idx >= 0 &&
           state.selected_route_idx < static_cast<int>(s_route_names.size()) &&
           !state.selected_route.empty();
}

std::string route_path_for_name(const std::string& route_name)
{
    return std::string(g_tracker_state.mode == TrackerPageState::Mode::Navigation ? platform::ui::navigation::directory() : platform::ui::route_storage::route_dir()) + "/" + route_name;
}

bool repair_degraded_active_route_from_list()
{
    auto& state = g_tracker_state;
    app::IAppFacade& app_ctx = app::appFacade();
    const auto& cfg = app_ctx.readConfig();
    if (!cfg.route_enabled || cfg.route_path[0] == '\0')
    {
        return false;
    }

    const std::string saved_path = cfg.route_path;
    if (!route_path_looks_degraded(saved_path))
    {
        return false;
    }
    if (s_route_names.size() != 1)
    {
        return false;
    }

    const std::string repaired_name = s_route_names.front();
    const std::string repaired_path = route_path_for_name(repaired_name);
    auto edit = app_ctx.beginConfigEdit();
    if (!edit)
    {
        return false;
    }
    std::strncpy(edit.config().route_path,
                 repaired_path.c_str(),
                 sizeof(edit.config().route_path) - 1);
    edit.config().route_path[sizeof(edit.config().route_path) - 1] = '\0';
    edit.config().route_enabled = true;
    edit.commit(app::AppConfigChangeSet::route());
    state.active_route = repaired_name;
    return true;
}

std::string route_asset_id_for_name(const std::string& route_name)
{
    if (route_name.size() == 70 && route_name.substr(66) == ".kml" && track_sharing::isInstalledPackageAssetId(std::string_view(route_name).substr(0, 66))) return route_name.substr(0, 66);
    std::uint32_t hash = 2166136261U;
    for (unsigned char ch : route_name)
    {
        hash ^= static_cast<std::uint32_t>(ch);
        hash *= 16777619U;
    }
    char text[16];
    std::snprintf(text, sizeof(text), "kml-%08lx", static_cast<unsigned long>(hash));
    return std::string(text);
}

std::string route_asset_root_for_id(const std::string& asset_id)
{
    return std::string(platform::ui::route_storage::route_dir()) + "/.trailmate/" + asset_id;
}

std::size_t find_case_insensitive(const std::string& text,
                                  const char* needle,
                                  std::size_t start = 0)
{
    if (!needle || needle[0] == '\0')
    {
        return std::string::npos;
    }
    const std::size_t needle_len = std::strlen(needle);
    if (needle_len > text.size())
    {
        return std::string::npos;
    }
    for (std::size_t pos = start; pos + needle_len <= text.size(); ++pos)
    {
        bool match = true;
        for (std::size_t index = 0; index < needle_len; ++index)
        {
            const unsigned char lhs = static_cast<unsigned char>(text[pos + index]);
            const unsigned char rhs = static_cast<unsigned char>(needle[index]);
            if (std::tolower(lhs) != std::tolower(rhs))
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return pos;
        }
    }
    return std::string::npos;
}

void extract_img_srcs(const std::string& text, std::vector<std::string>& out_urls)
{
    if (out_urls.size() >= kPreviewMaxImages) return;
    gps::kml::forEachImageSource(text, [&](std::string_view source)
                                 {
                                   out_urls.push_back(gps::kml::decodeImageSource(source));
                                   return out_urls.size() < kPreviewMaxImages; });
}

bool extract_tag_text(const std::string& line, const char* tag, std::string& out_text)
{
    out_text.clear();
    std::string open = "<";
    open += tag;
    const std::size_t open_pos = find_case_insensitive(line, open.c_str());
    if (open_pos == std::string::npos)
    {
        return false;
    }
    const std::size_t body_start = line.find('>', open_pos);
    if (body_start == std::string::npos)
    {
        return false;
    }
    std::string close = "</";
    close += tag;
    close += ">";
    const std::size_t close_pos = find_case_insensitive(line, close.c_str(), body_start + 1);
    if (close_pos == std::string::npos || close_pos <= body_start)
    {
        return false;
    }
    out_text = line.substr(body_start + 1, close_pos - body_start - 1);
    return true;
}

bool parse_comma_coordinate_token(const std::string& token,
                                  double& lat,
                                  double& lon,
                                  double& altitude_m,
                                  bool& has_altitude)
{
    const char* ptr = token.c_str();
    char* end = nullptr;
    const double parsed_lon = std::strtod(ptr, &end);
    if (end == ptr || *end != ',')
    {
        return false;
    }
    ptr = end + 1;
    const double parsed_lat = std::strtod(ptr, &end);
    if (end == ptr)
    {
        return false;
    }
    double parsed_alt = 0.0;
    bool parsed_has_alt = false;
    if (*end == ',')
    {
        ptr = end + 1;
        char* alt_end = nullptr;
        parsed_alt = std::strtod(ptr, &alt_end);
        parsed_has_alt = alt_end != ptr;
    }
    if (!std::isfinite(parsed_lat) || !std::isfinite(parsed_lon))
    {
        return false;
    }
    lat = parsed_lat;
    lon = parsed_lon;
    altitude_m = parsed_alt;
    has_altitude = parsed_has_alt && std::isfinite(parsed_alt);
    return true;
}

bool parse_space_coordinate_text(const std::string& text,
                                 double& lat,
                                 double& lon,
                                 double& altitude_m,
                                 bool& has_altitude)
{
    const char* ptr = text.c_str();
    char* end = nullptr;
    const double parsed_lon = std::strtod(ptr, &end);
    if (end == ptr)
    {
        return false;
    }
    ptr = end;
    const double parsed_lat = std::strtod(ptr, &end);
    if (end == ptr)
    {
        return false;
    }
    ptr = end;
    const double parsed_alt = std::strtod(ptr, &end);
    const bool parsed_has_alt = end != ptr;
    if (!std::isfinite(parsed_lat) || !std::isfinite(parsed_lon))
    {
        return false;
    }
    lat = parsed_lat;
    lon = parsed_lon;
    altitude_m = parsed_alt;
    has_altitude = parsed_has_alt && std::isfinite(parsed_alt);
    return true;
}

void append_preview_route_point(double lat, double lon, double altitude_m, bool has_altitude, bool break_before)
{
    if (!std::isfinite(lat) || !std::isfinite(lon))
    {
        return;
    }
    RoutePreviewPoint point{};
    point.lat = lat;
    point.lon = lon;
    point.altitude_m = altitude_m;
    point.has_altitude = has_altitude && std::isfinite(altitude_m);
    point.break_before = break_before;
    s_preview_route_points.push_back(point);
}

bool append_pending_images(std::string url,
                           double lat,
                           double lon,
                           bool has_position)
{
    if (s_preview_images.size() >= kPreviewMaxImages) return false;
    if (url.empty()) return true;
    RoutePreviewImage image{};
    image.source_hash_valid = platform::ui::route_storage::route_image_reference_hash(url, image.source_hash);
    image.url = std::move(url);
    image.lat = lat;
    image.lon = lon;
    image.has_position = has_position && std::isfinite(lat) && std::isfinite(lon);
    return s_preview_images.push_back(std::move(image));
}

double preview_degrees_to_radians(double degrees)
{
    return degrees * 0.017453292519943295;
}

double preview_distance_m(const RoutePreviewPoint& a, const RoutePreviewPoint& b)
{
    constexpr double kEarthRadiusM = 6371000.0;
    const double lat1 = preview_degrees_to_radians(a.lat);
    const double lat2 = preview_degrees_to_radians(b.lat);
    const double dlat = preview_degrees_to_radians(b.lat - a.lat);
    const double dlon = preview_degrees_to_radians(b.lon - a.lon);
    const double sin_lat = std::sin(dlat / 2.0);
    const double sin_lon = std::sin(dlon / 2.0);
    const double h = (sin_lat * sin_lat) +
                     std::cos(lat1) * std::cos(lat2) * (sin_lon * sin_lon);
    return 2.0 * kEarthRadiusM * std::atan2(std::sqrt(h), std::sqrt(std::max(0.0, 1.0 - h)));
}

void refresh_preview_metrics()
{
    s_preview_metrics = RoutePreviewMetrics{};
    bool have_altitude = false;
    double previous_altitude = 0.0;
    bool have_previous = false;
    const RoutePreviewPoint* previous_point = nullptr;
    for (const auto& point : s_preview_route_points)
    {
        if (point.break_before) previous_point = nullptr, have_previous = false;
        if (previous_point)
        {
            const double distance = preview_distance_m(*previous_point, point);
            if (std::isfinite(distance))
            {
                s_preview_metrics.distance_m += distance;
            }
        }
        previous_point = &point;

        if (!point.has_altitude || !std::isfinite(point.altitude_m))
        {
            have_previous = false;
            continue;
        }
        if (!have_altitude)
        {
            s_preview_metrics.min_altitude_m = point.altitude_m;
            s_preview_metrics.max_altitude_m = point.altitude_m;
            have_altitude = true;
        }
        else
        {
            s_preview_metrics.min_altitude_m =
                std::min(s_preview_metrics.min_altitude_m, point.altitude_m);
            s_preview_metrics.max_altitude_m =
                std::max(s_preview_metrics.max_altitude_m, point.altitude_m);
        }
        if (have_previous)
        {
            const double delta = point.altitude_m - previous_altitude;
            if (delta > 0.0)
            {
                s_preview_metrics.ascent_m += delta;
            }
            else
            {
                s_preview_metrics.descent_m += -delta;
            }
        }
        previous_altitude = point.altitude_m;
        have_previous = true;
        ++s_preview_metrics.altitude_count;
    }
}

void assign_preview_image_paths()
{
    const std::string asset_root = route_asset_root_for_id(s_preview_asset_id);
    for (std::size_t index = 0; index < s_preview_images.size(); ++index)
    {
        char local_name[32];
        char preview_name[36];
        char view_name[34];
        const unsigned display_index = static_cast<unsigned>(index + 1);
        std::snprintf(local_name, sizeof(local_name), "/images/img-%04u.jpg", display_index);
        std::snprintf(preview_name, sizeof(preview_name), "/thumbs/thumb-%04u.bmp", display_index);
        std::snprintf(view_name, sizeof(view_name), "/views/view-%04u.bmp", display_index);
        s_preview_images[index].local_path = asset_root + local_name;
        s_preview_images[index].preview_path = asset_root + preview_name;
        s_preview_images[index].view_path = asset_root + view_name;
    }
}

void refresh_preview_saved_image_count_hint()
{
    assign_preview_image_paths();
    s_preview_saved_image_count_hint = 0;
    s_preview_image_saved_state_known = false;
    if (s_preview_asset_id.empty() || s_preview_images.empty())
    {
        return;
    }

    for (auto& image : s_preview_images)
    {
        ::ui::support::refreshRouteImageSourceState(image, false);
        if (image.downloaded) ++s_preview_saved_image_count_hint;
    }
    s_preview_image_saved_state_known = true;
}

void refresh_preview_image_storage_state(bool include_cache_state)
{
    assign_preview_image_paths();
    std::size_t saved_count = 0;
    for (RoutePreviewImage& image : s_preview_images)
    {
        ::ui::support::refreshRouteImageSourceState(image, include_cache_state);
        if (image.downloaded)
        {
            ++saved_count;
        }
        image.view_ready = false;
    }
    s_preview_saved_image_count_hint = saved_count;
    s_preview_image_saved_state_known = true;
    s_preview_image_cache_state_known = include_cache_state;
}

bool apply_route_preview_download_status_to_images(
    const platform::ui::route_storage::RouteImageDownloadStatus& status)
{
    assign_preview_image_paths();
    if (s_preview_asset_id.empty() ||
        status.asset_id.empty() ||
        status.asset_id != s_preview_asset_id)
    {
        return false;
    }

    if (status.phase == platform::ui::route_storage::RouteImageDownloadPhase::Done ||
        status.phase == platform::ui::route_storage::RouteImageDownloadPhase::Failed)
    {
        refresh_preview_image_storage_state(true);
        return true;
    }

    // A batch may contain only missing images. Its first N successes are not
    // the first N global image slots, and counters are not source proofs.
    // Keep progress display separate; inspect actual records at termination.
    return false;
}

void clear_preview_model()
{
    if (s_preview_native_timer) lv_timer_del(s_preview_native_timer);
    s_preview_native_timer = nullptr;
    s_preview_map_controls = nullptr;
    ::ui::support::RouteUiMemory::destroy(s_preview_native_route);
    s_preview_view.reset();
    s_preview_route_points.clear();
    s_preview_images.clear();
    s_preview_map_line_points.clear();
    s_preview_elevation_samples.clear();
    s_preview_image_strip_items.clear();
    ::ui::widgets::route_elevation_profile::reset(s_preview_elevation_profile);
    s_preview_metrics = RoutePreviewMetrics{};
    s_preview_route_name.clear();
    s_preview_asset_id.clear();
    s_preview_status_text.clear();
    s_preview_download_state = RoutePreviewDownloadState::Idle;
    s_preview_selected_image = 0;
    s_preview_elevation_visible = false;
    s_preview_image_strip_visible = false;
    s_preview_image_saved_state_known = false;
    s_preview_image_cache_state_known = false;
    s_preview_saved_image_count_hint = 0;
}

bool load_route_preview_model(const std::string& route_name)
{
    clear_preview_model();
    if (route_name.empty()) return false;
    s_preview_route_name = route_name;
    s_preview_asset_id = route_asset_id_for_name(route_name);
    const std::string path = route_path_for_name(route_name);
    if (path.size() >= 8 && path.compare(path.size() - 8, 8, ".tmroute") == 0)
    {
        auto result = ::ui::support::projectTmRoutePreview(path.c_str(), s_preview_route_points, s_preview_metrics.distance_m);
        if (result == tmap::Result::Ok && !s_preview_route_points.empty())
        {
            s_preview_native_route = ::ui::support::RouteUiMemory::create<::ui::support::TmRouteMap>();
            if (s_preview_native_route && s_preview_native_route->begin(path.c_str())) return true;
            result = tmap::Result::BudgetExceeded;
        }
        clear_preview_model();
        s_preview_status_text = ::ui::i18n::tr(result == tmap::Result::Busy ? "SD busy; retry preview" : result == tmap::Result::BudgetExceeded ? "Route preview budget exceeded"
                                                                                                                                                : "Route file unavailable or damaged");
        return false;
    }
    ::ui::support::KmlElevationMetrics metrics;
    if (!::ui::support::projectKmlRoute(path.c_str(), s_preview_route_points, s_preview_elevation_samples, metrics, 96))
    {
        clear_preview_model();
        return false;
    }
    size_t feature_start = 0;
    const track_sharing::KmlImageCallbacks callbacks{
        &feature_start,
        [](void* p)
        { *static_cast<size_t*>(p) = s_preview_images.size(); },
        [](void*, std::string_view source)
        {
            if (s_preview_images.size() >= kPreviewMaxImages || s_preview_images.failed()) return false;
            return append_pending_images(gps::kml::decodeImageSource(source), 0, 0, false);
        },
        [](void* p, bool located, double lat, double lon)
        {
            for (size_t i = *static_cast<size_t*>(p); i < s_preview_images.size(); ++i)
            {
                s_preview_images[i].has_position = located;
                s_preview_images[i].lat = lat;
                s_preview_images[i].lon = lon;
            }
        }};
    if (!::ui::support::readRouteImageKml(path.c_str(), callbacks))
    {
        clear_preview_model();
        return false;
    }
    s_preview_metrics.altitude_count = metrics.altitude_count;
    s_preview_metrics.min_altitude_m = metrics.min_altitude_m;
    s_preview_metrics.max_altitude_m = metrics.max_altitude_m;
    s_preview_metrics.ascent_m = metrics.ascent_m;
    s_preview_metrics.descent_m = metrics.descent_m;
    s_preview_metrics.distance_m = s_preview_elevation_samples.empty() ? 0 : s_preview_elevation_samples.back().distance_m;
    assign_preview_image_paths();
    refresh_preview_saved_image_count_hint();
    if (!s_preview_images.empty()) s_preview_selected_image = 0;
    return !s_preview_route_points.empty() || !s_preview_images.empty();
}

struct PreviewBounds
{
    double min_lat = 0.0;
    double max_lat = 0.0;
    double min_lon = 0.0;
    double max_lon = 0.0;
    bool valid = false;
};

void include_preview_bounds(PreviewBounds& bounds, double lat, double lon)
{
    if (!std::isfinite(lat) || !std::isfinite(lon))
    {
        return;
    }
    if (!bounds.valid)
    {
        bounds.min_lat = lat;
        bounds.max_lat = lat;
        bounds.min_lon = lon;
        bounds.max_lon = lon;
        bounds.valid = true;
        return;
    }
    bounds.min_lat = std::min(bounds.min_lat, lat);
    bounds.max_lat = std::max(bounds.max_lat, lat);
    bounds.min_lon = std::min(bounds.min_lon, lon);
    bounds.max_lon = std::max(bounds.max_lon, lon);
}

PreviewBounds compute_preview_bounds()
{
    PreviewBounds bounds{};
    for (const auto& point : s_preview_route_points)
    {
        include_preview_bounds(bounds, point.lat, point.lon);
    }
    for (const auto& image : s_preview_images)
    {
        if (image.has_position)
        {
            include_preview_bounds(bounds, image.lat, image.lon);
        }
    }
    return bounds;
}

bool preview_model_fits_bounds(lv_obj_t* viewport_root,
                               const ::ui::widgets::map::Model& model,
                               const PreviewBounds& bounds)
{
    if (!viewport_root || !lv_obj_is_valid(viewport_root) || !bounds.valid)
    {
        return false;
    }

    lv_obj_update_layout(viewport_root);
    const lv_coord_t width = lv_obj_get_content_width(viewport_root);
    const lv_coord_t height = lv_obj_get_content_height(viewport_root);
    const lv_coord_t margin = std::max<lv_coord_t>(10, std::min(width, height) / 10);

    const ::ui::widgets::map::GeoPoint corners[] = {
        {true, bounds.min_lat, bounds.min_lon},
        {true, bounds.min_lat, bounds.max_lon},
        {true, bounds.max_lat, bounds.min_lon},
        {true, bounds.max_lat, bounds.max_lon},
    };
    for (const auto& geo : corners)
    {
        lv_point_t point{};
        if (!::ui::widgets::map::preview_project_point(viewport_root, model, geo, point))
        {
            return false;
        }
        if (point.x < margin ||
            point.y < margin ||
            point.x > width - margin ||
            point.y > height - margin)
        {
            return false;
        }
    }
    return true;
}

::ui::widgets::map::Model build_preview_map_model()
{
    ::ui::widgets::map::Model model{};
    const PreviewBounds bounds = compute_preview_bounds();
    const auto layers = ::ui::widgets::map::current_layer_state();
    model.map_source = layers.map_source;
    model.contour_enabled = layers.contour_enabled;
    model.coord_system = app::configFacade().readConfig().map_coord_system;
    model.zoom = ::ui::widgets::map::kDefaultZoom;
    model.focus_point.valid = bounds.valid;
    model.focus_point.lat = bounds.valid ? (bounds.min_lat + bounds.max_lat) / 2.0 : 0.0;
    model.focus_point.lon = bounds.valid ? (bounds.min_lon + bounds.max_lon) / 2.0 : 0.0;
    if (s_preview_view.zoom >= 0)
    {
        model.zoom = s_preview_view.zoom;
        model.pan_x = s_preview_view.pan_x;
        model.pan_y = s_preview_view.pan_y;
        return model;
    }
    if (!bounds.valid || !g_tracker_state.route_preview_map_host)
    {
        return model;
    }

    model.zoom = ::ui::widgets::map::kMinZoom;
    for (int zoom = ::ui::widgets::map::kMaxZoom;
         zoom >= ::ui::widgets::map::kMinZoom;
         --zoom)
    {
        model.zoom = zoom;
        if (preview_model_fits_bounds(g_tracker_state.route_preview_map_host, model, bounds))
        {
            break;
        }
    }
    s_preview_view.zoom = model.zoom;
    return model;
}

bool build_preview_map_line_points()
{
    s_preview_map_line_points.clear();
    bool disconnected = true;
    bool admitted = true;
    ::ui::support::sampleRouteSegments(
        s_preview_route_points.data(), s_preview_route_points.size(), kPreviewMaxDrawPoints,
        [](const RoutePreviewPoint& point)
        { return std::isfinite(point.lat) && std::isfinite(point.lon); },
        [&](const RoutePreviewPoint& point, bool connect)
        {
            lv_point_t projected{};
            if (!::ui::widgets::map::project_point(s_preview_map_runtime, {true, point.lat, point.lon}, projected))
            {
                disconnected = true;
                return true;
            }
            if ((!connect || disconnected) && !s_preview_map_line_points.empty())
                admitted = s_preview_map_line_points.push_back(::ui::support::routeLineBreak());
            if (admitted)
            {
                lv_point_precise_t precise{};
                precise.x = static_cast<float>(projected.x);
                precise.y = static_cast<float>(projected.y);
                admitted = s_preview_map_line_points.push_back(std::move(precise));
            }
            disconnected = false;
            return admitted;
        });
    if (!admitted) s_preview_map_line_points.clear();
    return s_preview_map_line_points.size() >= 2;
}

void draw_preview_map_overlay()
{
    const auto& widgets = ::ui::widgets::map::widgets(s_preview_map_runtime);
    if (!widgets.overlay_layer || !lv_obj_is_valid(widgets.overlay_layer))
    {
        return;
    }
    lv_obj_clean(widgets.overlay_layer);

    if (s_preview_native_route)
    {
        // A child follows the viewport's lightweight translation during a drag.
        // Its draw callback consumes only the existing fixed screen snapshot.
        auto* line = lv_obj_create(widgets.overlay_layer);
        lv_obj_set_size(line, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(line, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(line, 0, 0);
        lv_obj_set_style_pad_all(line, 0, 0);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
        s_preview_native_route->attach(line);
    }
    else if (build_preview_map_line_points())
    {
        lv_obj_t* line = lv_line_create(widgets.overlay_layer);
        lv_obj_set_size(line, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_line_width(line, 3, LV_PART_MAIN);
        lv_obj_set_style_line_color(line, lv_color_hex(0xE94F2E), LV_PART_MAIN);
        lv_obj_set_style_line_rounded(line, true, LV_PART_MAIN);
        lv_obj_add_event_cb(
            line, [](lv_event_t* event)
            { ::ui::support::drawDisconnectedRouteLine(event, s_preview_map_line_points.data(), s_preview_map_line_points.size()); },
            LV_EVENT_DRAW_MAIN, nullptr);
    }

    for (std::size_t index = 0; index < s_preview_images.size(); ++index)
    {
        const auto& image = s_preview_images[index];
        if (!image.has_position)
        {
            continue;
        }
        lv_point_t point{};
        if (!::ui::widgets::map::project_point(
                s_preview_map_runtime,
                {true, image.lat, image.lon},
                point))
        {
            continue;
        }
        const bool selected = static_cast<int>(index) == s_preview_selected_image;
        lv_obj_t* pin = lv_btn_create(widgets.overlay_layer);
        lv_obj_set_size(pin, selected ? 14 : 11, selected ? 14 : 11);
        lv_obj_set_pos(pin,
                       point.x - (selected ? 7 : 5),
                       point.y - (selected ? 13 : 10));
        lv_obj_set_style_radius(pin, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(pin, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(pin, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_bg_color(
            pin,
            selected ? lv_color_hex(0xF0442E) : lv_color_hex(0xF4A11E),
            LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pin, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(pin, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(pin, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_group_remove_obj(pin);
        lv_obj_set_user_data(pin, reinterpret_cast<void*>(index + 1));
        lv_obj_add_event_cb(
            pin,
            [](lv_event_t* e)
            {
                lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
                const std::size_t raw = static_cast<std::size_t>(
                    reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target)));
                if (raw > 0 && raw <= s_preview_images.size())
                {
                    s_preview_selected_image = static_cast<int>(raw - 1);
                    s_preview_status_text.clear();
                    sync_route_preview_download_status();
                    refresh_route_preview_map();
                    update_route_preview_status();
                }
            },
            LV_EVENT_CLICKED,
            nullptr);
    }
}

bool project_preview_native_route(void*, tmap::GeoPoint point, tmap::ScreenPoint& output)
{
    lv_point_t pixel;
    if (!::ui::widgets::map::project_point(s_preview_map_runtime, {true, point.latitude / 1e7, point.longitude / 1e7}, pixel)) return false;
    output.x = static_cast<float>(pixel.x);
    output.y = static_cast<float>(pixel.y);
    return true;
}

void sync_preview_native_view()
{
    if (!s_preview_native_route) return;
    auto* host = g_tracker_state.route_preview_map_host;
    ::ui::widgets::map::GeoPoint center;
    if (!host || !::ui::widgets::map::screen_center(s_preview_map_runtime, center)) return;
    tmap::MapViewport view;
    view.center = {static_cast<int32_t>(std::llround(center.lat * 1e7)), static_cast<int32_t>(std::llround(center.lon * 1e7))};
    view.zoom = static_cast<uint8_t>(s_preview_view.zoom);
    view.width = static_cast<float>(lv_obj_get_content_width(host));
    view.height = static_cast<float>(lv_obj_get_content_height(host));
    if (view.width <= 0 || view.height <= 0) return;
    s_preview_native_route->setViewport(view, project_preview_native_route, nullptr,
                                        app::configFacade().readConfig().map_coord_system ? 200000 : 0);
}

void preview_map_gesture(const ::ui::widgets::map::GestureEvent& event, void*)
{
    using Phase = ::ui::widgets::map::GesturePhase;
    if (event.phase == Phase::Tapped)
    {
        const auto* overlay = ::ui::widgets::map::widgets(s_preview_map_runtime).overlay_layer;
        if (!overlay) return;
        lv_area_t origin;
        lv_obj_get_coords(overlay, &origin);
        for (std::size_t i = s_preview_images.size(); i > 0; --i)
        {
            const auto& image = s_preview_images[i - 1];
            lv_point_t point;
            if (!image.has_position || !::ui::widgets::map::project_point(s_preview_map_runtime, {true, image.lat, image.lon}, point)) continue;
            if (std::abs(event.point.x - origin.x1 - point.x) > 12 || std::abs(event.point.y - origin.y1 - (point.y - 6)) > 12) continue;
            s_preview_selected_image = static_cast<int>(i - 1);
            s_preview_status_text.clear();
            sync_route_preview_download_status();
            refresh_route_preview_map();
            update_route_preview_status();
            break;
        }
    }
    else if (event.phase == Phase::DragBegin) s_preview_view.beginDrag();
    else if (event.phase == Phase::DragUpdate)
    {
        s_preview_view.drag(event.total_dx, event.total_dy);
        ::ui::widgets::map::apply_model_lightweight(s_preview_map_runtime, build_preview_map_model());
    }
    else if (event.phase == Phase::DragEnd || event.phase == Phase::Cancel)
        refresh_route_preview_map();
}

void refresh_route_preview_map()
{
    auto& state = g_tracker_state;
    if (!state.route_preview_map_host ||
        !lv_obj_is_valid(state.route_preview_map_host))
    {
        return;
    }
    lv_obj_update_layout(state.route_preview_map_host);
    const lv_coord_t width = lv_obj_get_content_width(state.route_preview_map_host);
    const lv_coord_t height = lv_obj_get_content_height(state.route_preview_map_host);
    if (width <= 0 || height <= 0)
    {
        return;
    }

    if (!::ui::widgets::map::widgets(s_preview_map_runtime).root)
    {
        (void)::ui::widgets::map::create(s_preview_map_runtime, state.route_preview_map_host, 180);
        ::ui::widgets::map::set_gesture_callback(s_preview_map_runtime, preview_map_gesture, nullptr);
        ::ui::widgets::map::set_gesture_enabled(s_preview_map_runtime, true);
        s_preview_map_loader_paused = false;
    }
    sync_route_preview_map_loader_pause();
    ::ui::widgets::map::set_size(s_preview_map_runtime, width, height);
    ::ui::widgets::map::apply_model(s_preview_map_runtime, build_preview_map_model());
    draw_preview_map_overlay();
    sync_preview_native_view();
    create_preview_map_controls();
    if (s_preview_native_route && !s_preview_native_timer)
        s_preview_native_timer = lv_timer_create([](lv_timer_t*)
                                                 {
            auto* page = g_tracker_state.route_preview_page;
            if (!page || !lv_obj_is_valid(page) || !lv_obj_is_visible(page) || !s_preview_native_route) return;
            const bool changed = s_preview_native_route->poll(false, {}, lv_tick_get(), 0, false, lv_tick_get);
            if (changed && s_preview_native_route->dense())
                show_route_preview_notice("Zoom in to show route detail", 2000);
            else if (changed && s_preview_native_route->error() != tmap::Result::Ok && s_preview_native_route->error() != tmap::Result::More)
                show_route_preview_notice("Route file unavailable or damaged", 2500); },
                                                 50, nullptr);
    sync_route_preview_image_strip();
}

void set_plain_panel(lv_obj_t* obj, uint32_t bg)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

::ui::widgets::route_image_strip::Config route_preview_image_strip_config()
{
    ::ui::widgets::route_image_strip::Config config{};
    config.width = 200;
    config.item_height = page_profile().dense ? 104 : 120;
    return config;
}

uint32_t route_preview_hash_byte(uint32_t hash, uint8_t value)
{
    hash ^= value;
    hash *= 16777619U;
    return hash;
}

uint32_t route_preview_hash_string(uint32_t hash, const std::string& value)
{
    for (unsigned char ch : value)
    {
        hash = route_preview_hash_byte(hash, ch);
    }
    return route_preview_hash_byte(hash, 0);
}

uint32_t route_preview_image_strip_items_hash()
{
    uint32_t hash = 2166136261U;
    hash = route_preview_hash_string(hash, s_preview_asset_id);
    for (const auto& image : s_preview_images)
    {
        hash = route_preview_hash_byte(hash, image.downloaded ? 1U : 0U);
        hash = route_preview_hash_byte(hash, image.preview_ready ? 1U : 0U);
        hash = route_preview_hash_byte(hash, image.view_ready ? 1U : 0U);
    }
    return hash;
}

void rebuild_route_preview_image_strip_items()
{
    s_preview_image_strip_items.clear();
    s_preview_image_strip_items.reserve(s_preview_images.size());
    for (const auto& image : s_preview_images)
    {
        ::ui::widgets::route_image_strip::Item item{};
        item.local_path = image.local_path;
        item.preview_path = image.preview_ready ? image.preview_path : std::string{};
        item.view_path = image.view_ready ? image.view_path : std::string{};
        item.downloaded = image.downloaded;
        s_preview_image_strip_items.push_back(std::move(item));
    }
}

void on_route_preview_image_strip_selected(std::size_t index, void*)
{
    if (index >= s_preview_images.size())
    {
        return;
    }
    s_preview_selected_image = static_cast<int>(index);
    s_preview_status_text.clear();
    draw_preview_map_overlay();
    update_route_preview_status();
}

void destroy_route_preview_image_strip(bool reset_visibility)
{
    ::ui::widgets::route_image_strip::destroy(s_preview_image_strip);
    ::ui::widgets::route_image_strip::reset(s_preview_image_strip);
    s_preview_image_strip_items.clear();
    s_preview_image_strip_items_hash = 0;
    if (reset_visibility)
    {
        s_preview_image_strip_visible = false;
    }
}

void sync_route_preview_image_strip()
{
    auto& state = g_tracker_state;
    if (!state.route_preview_map_host ||
        !lv_obj_is_valid(state.route_preview_map_host))
    {
        destroy_route_preview_image_strip(true);
        return;
    }

    if (s_preview_images.empty())
    {
        destroy_route_preview_image_strip(true);
        return;
    }

    if (!s_preview_image_strip_visible && !s_preview_image_strip.root)
    {
        return;
    }

    ::ui::widgets::route_image_strip::create(
        state.route_preview_map_host,
        s_preview_image_strip,
        route_preview_image_strip_config());
    ::ui::widgets::route_image_strip::set_selection_callback(
        s_preview_image_strip,
        on_route_preview_image_strip_selected,
        nullptr);
    bind_route_preview_key_handler(s_preview_image_strip.root);
    const uint32_t next_items_hash = route_preview_image_strip_items_hash();
    if (next_items_hash != s_preview_image_strip_items_hash ||
        s_preview_image_strip.items.empty())
    {
        rebuild_route_preview_image_strip_items();
        ::ui::widgets::route_image_strip::set_items(
            s_preview_image_strip,
            s_preview_image_strip_items.data(),
            s_preview_image_strip_items.size());
        s_preview_image_strip_items_hash = next_items_hash;
    }
    if (!s_preview_images.empty())
    {
        const std::size_t selected =
            static_cast<std::size_t>(std::max(0, s_preview_selected_image));
        ::ui::widgets::route_image_strip::set_selected(
            s_preview_image_strip,
            std::min<std::size_t>(selected, s_preview_images.size() - 1),
            false);
    }
    ::ui::widgets::route_image_strip::set_hidden(
        s_preview_image_strip,
        !s_preview_image_strip_visible);
}

void toggle_route_preview_image_strip()
{
    assign_preview_image_paths();
    if (s_preview_images.empty())
    {
        s_preview_status_text = s_preview_images.failed() ? ::ui::i18n::tr("Image memory unavailable") : "No route images";
        s_preview_image_strip_visible = false;
        sync_route_preview_image_strip();
        update_route_preview_status();
        return;
    }
    const bool next_visible = !s_preview_image_strip_visible;
    s_preview_image_strip_visible = next_visible;
    s_preview_status_text =
        s_preview_image_strip_visible ? "Images shown" : "Images hidden";
    if (s_preview_image_strip_visible)
    {
        ensure_route_preview_image_cache_build();
        if (::ui::support::routeImagesNeedSourceReview(s_preview_images))
            show_route_preview_notice(::ui::i18n::tr("Image cache needs checking. Download again."), 2200);
    }
    sync_route_preview_image_strip();
    update_route_preview_status();
}

std::size_t preview_saved_image_count()
{
    return static_cast<std::size_t>(
        std::count_if(s_preview_images.begin(),
                      s_preview_images.end(),
                      [](const RoutePreviewImage& image)
                      {
                          return image.downloaded;
                      }));
}

std::size_t route_preview_saved_image_count()
{
    if (s_preview_image_saved_state_known)
    {
        return std::min<std::size_t>(s_preview_saved_image_count_hint,
                                     s_preview_images.size());
    }
    return preview_saved_image_count();
}

bool route_preview_image_cache_ready(const RoutePreviewImage& image)
{
    if (!image.downloaded)
    {
        return false;
    }
    return image.preview_path.empty() || image.preview_ready;
}

std::size_t preview_cached_image_count()
{
    return static_cast<std::size_t>(
        std::count_if(s_preview_images.begin(),
                      s_preview_images.end(),
                      [](const RoutePreviewImage& image)
                      {
                          return route_preview_image_cache_ready(image);
                      }));
}

bool route_preview_all_image_caches_ready()
{
    return s_preview_image_cache_state_known &&
           !s_preview_images.empty() &&
           preview_cached_image_count() >= s_preview_images.size();
}

std::vector<platform::ui::route_storage::RouteImageCacheItem> route_preview_cache_items()
{
    std::vector<platform::ui::route_storage::RouteImageCacheItem> items;
    items.reserve(s_preview_images.size());
    const bool package = track_sharing::isInstalledPackageRouteAtRoot(route_path_for_name(s_preview_route_name), platform::ui::route_storage::route_dir());
    for (const auto& image : s_preview_images)
    {
        if (!image.downloaded && !package)
        {
            continue;
        }
        platform::ui::route_storage::RouteImageCacheItem item{};
        item.source_path = image.local_path;
        item.preview_path = image.preview_path;
        item.view_path = image.view_path;
        std::memcpy(item.source_hash, image.source_hash, 32);
        item.source_hash_valid = image.source_hash_valid;
        items.push_back(std::move(item));
    }
    return items;
}

bool route_preview_download_status_matches(
    const platform::ui::route_storage::RouteImageDownloadStatus& status)
{
    return !s_preview_asset_id.empty() &&
           !status.asset_id.empty() &&
           status.asset_id == s_preview_asset_id;
}

RoutePreviewDownloadState map_route_preview_download_state(
    platform::ui::route_storage::RouteImageDownloadPhase phase)
{
    using Phase = platform::ui::route_storage::RouteImageDownloadPhase;
    switch (phase)
    {
    case Phase::Downloading:
    case Phase::Caching:
        return RoutePreviewDownloadState::Downloading;
    case Phase::Done:
        return RoutePreviewDownloadState::Done;
    case Phase::Failed:
        return RoutePreviewDownloadState::Failed;
    case Phase::Idle:
    default:
        return RoutePreviewDownloadState::Idle;
    }
}

bool route_preview_download_busy_for_current_route()
{
    const auto status = platform::ui::route_storage::route_image_download_status();
    return route_preview_download_status_matches(status) && status.busy;
}

void sync_route_preview_map_loader_pause(
    const platform::ui::route_storage::RouteImageDownloadStatus& status)
{
    const bool should_pause = route_preview_download_status_matches(status) && status.busy;
    if (s_preview_map_loader_paused == should_pause)
    {
        return;
    }

    s_preview_map_loader_paused = should_pause;
    ::ui::widgets::map::set_loader_paused(s_preview_map_runtime, should_pause);
}

void sync_route_preview_map_loader_pause()
{
    sync_route_preview_map_loader_pause(
        platform::ui::route_storage::route_image_download_status());
}

bool route_preview_all_images_saved()
{
    return s_preview_image_saved_state_known &&
           !s_preview_images.empty() &&
           route_preview_saved_image_count() >= s_preview_images.size();
}

bool route_preview_load_button_disabled()
{
    if (preview_selected_route_is_active())
    {
        return false;
    }
    if (!g_tracker_state.active_route.empty())
    {
        return true;
    }

    const auto status = platform::ui::route_storage::route_image_download_status();
    if (route_preview_download_status_matches(status))
    {
        if (status.busy || status.phase == platform::ui::route_storage::RouteImageDownloadPhase::Done)
        {
            return true;
        }
        if (status.phase == platform::ui::route_storage::RouteImageDownloadPhase::Failed)
        {
            return false;
        }
    }
    return route_preview_all_image_caches_ready();
}

bool route_preview_download_button_disabled()
{
    if (s_preview_images.empty())
    {
        return true;
    }
    const auto status = platform::ui::route_storage::route_image_download_status();
    if (route_preview_download_status_matches(status) && status.busy)
    {
        return true;
    }
    return route_preview_all_images_saved();
}

void show_route_preview_notice(const char* message, uint32_t duration_ms = 1800)
{
    if (message && message[0] != '\0')
    {
        ::ui::feedback::show_notice(message, duration_ms);
    }
}

void set_button_disabled(lv_obj_t* btn, bool disabled)
{
    if (!btn || !lv_obj_is_valid(btn))
    {
        return;
    }
    if (disabled)
    {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_clear_state(btn, LV_STATE_DISABLED);
    }
}

void update_route_preview_buttons()
{
    auto& state = g_tracker_state;
    set_button_disabled(state.route_preview_download_btn, route_preview_download_button_disabled());
    set_button_disabled(state.route_preview_load_btn, route_preview_load_button_disabled());
}

void sync_route_preview_download_status()
{
    const auto status = platform::ui::route_storage::route_image_download_status();
    ::ui::widgets::route_image_operation::sync(status);
    sync_route_preview_map_loader_pause(status);
    if (!route_preview_download_status_matches(status))
    {
        assign_preview_image_paths();
        sync_route_preview_image_strip();
        update_route_preview_buttons();
        if (!status.busy && s_preview_download_poll_timer)
        {
            lv_timer_pause(s_preview_download_poll_timer);
        }
        return;
    }

    const RoutePreviewDownloadState previous_state = s_preview_download_state;
    s_preview_download_state = map_route_preview_download_state(status.phase);
    if (status.total > 0)
    {
        s_preview_selected_image = static_cast<int>(
            std::min<std::size_t>(status.current_index, status.total - 1));
    }
    s_preview_status_text = !status.message.empty() ? status.message : status.error;
    const bool download_just_finished =
        previous_state == RoutePreviewDownloadState::Downloading && !status.busy;
    const bool should_start_cache_after_download =
        download_just_finished &&
        status.phase == platform::ui::route_storage::RouteImageDownloadPhase::Done;
    if (download_just_finished)
    {
        refresh_preview_image_storage_state(true);
    }
    else
    {
        apply_route_preview_download_status_to_images(status);
    }
    sync_route_preview_image_strip();
    update_route_preview_status();
    update_route_preview_buttons();

    if (!status.busy &&
        previous_state == RoutePreviewDownloadState::Downloading &&
        s_preview_download_refresh_map_on_finish)
    {
        s_preview_download_refresh_map_on_finish = false;
        if (s_preview_elevation_visible)
        {
            sync_route_preview_elevation_visibility();
        }
        else
        {
            refresh_route_preview_map();
        }
    }

    if (should_start_cache_after_download &&
        route_preview_all_images_saved() &&
        !route_preview_all_image_caches_ready())
    {
        ensure_route_preview_image_cache_build(false);
        return;
    }

    if (!status.busy && s_preview_download_poll_timer)
    {
        lv_timer_pause(s_preview_download_poll_timer);
    }
}

void route_preview_download_poll_cb(lv_timer_t*)
{
    sync_route_preview_download_status();
}

void ensure_route_preview_download_poll_timer()
{
    if (s_preview_download_poll_timer)
    {
        lv_timer_resume(s_preview_download_poll_timer);
        return;
    }
    s_preview_download_poll_timer =
        lv_timer_create(route_preview_download_poll_cb, kPreviewDownloadPollMs, nullptr);
    if (s_preview_download_poll_timer)
    {
        lv_timer_set_repeat_count(s_preview_download_poll_timer, -1);
    }
}

void pause_route_preview_download_poll_timer()
{
    if (s_preview_download_poll_timer)
    {
        lv_timer_pause(s_preview_download_poll_timer);
    }
}

void ensure_route_preview_image_cache_build(bool refresh_state)
{
    const auto path = route_path_for_name(s_preview_route_name);
    const bool package = track_sharing::isInstalledPackageRouteAtRoot(path, platform::ui::route_storage::route_dir());
    if (refresh_state)
    {
        refresh_preview_image_storage_state(true);
    }
    if (s_preview_images.empty() ||
        (!route_preview_all_images_saved() && !package) ||
        route_preview_all_image_caches_ready())
    {
        return;
    }

    const auto status = platform::ui::route_storage::route_image_download_status();
    if (status.busy)
    {
        if (route_preview_download_status_matches(status))
        {
            ensure_route_preview_download_poll_timer();
            sync_route_preview_download_status();
        }
        else
        {
            s_preview_status_text = "Image task busy";
            update_route_preview_status();
            update_route_preview_buttons();
        }
        return;
    }

    std::string asset_dir;
    if (!platform::ui::route_storage::ensure_route_asset_dir(s_preview_asset_id, asset_dir))
    {
        s_preview_download_state = RoutePreviewDownloadState::Failed;
        s_preview_status_text = "Create image dir failed";
        update_route_preview_status();
        update_route_preview_buttons();
        return;
    }

    auto items = route_preview_cache_items();
    if (items.empty())
    {
        return;
    }

    std::string error;
    if (!platform::ui::route_storage::start_route_image_cache_build(
            s_preview_asset_id,
            std::move(items),
            error,
            platform::ui::route_storage::RouteImageTaskPresentation::UserVisible,
            package ? path : std::string{}))
    {
        s_preview_download_state = RoutePreviewDownloadState::Failed;
        s_preview_status_text = error.empty() ? "Start cache failed" : error;
        update_route_preview_status();
        update_route_preview_buttons();
        return;
    }

    s_preview_download_state = RoutePreviewDownloadState::Downloading;
    s_preview_status_text = "Preparing image cache";
    ensure_route_preview_download_poll_timer();
    sync_route_preview_download_status();
    update_route_preview_status();
    update_route_preview_buttons();
}

void update_route_preview_status()
{
    auto& state = g_tracker_state;
    const std::size_t saved_count = route_preview_saved_image_count();
    const std::size_t cached_count = preview_cached_image_count();
    const auto download_status = platform::ui::route_storage::route_image_download_status();

    if (state.route_preview_status_label &&
        lv_obj_is_valid(state.route_preview_status_label))
    {
        std::size_t display_saved_count = saved_count;
        if (route_preview_download_status_matches(download_status) &&
            download_status.busy &&
            download_status.total > 0 &&
            download_status.total < s_preview_images.size())
        {
            display_saved_count = std::min<std::size_t>(
                s_preview_images.size(),
                saved_count + download_status.saved);
        }
        if (s_preview_images.empty())
        {
            lv_obj_add_flag(state.route_preview_status_label, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            char status[24]{};
            const bool live_download_status =
                route_preview_download_status_matches(download_status) &&
                download_status.busy;
            if (s_preview_image_saved_state_known ||
                live_download_status)
            {
                std::snprintf(status,
                              sizeof(status),
                              "%u/%u",
                              static_cast<unsigned>(display_saved_count),
                              static_cast<unsigned>(s_preview_images.size()));
            }
            else
            {
                std::snprintf(status,
                              sizeof(status),
                              "--/%u",
                              static_cast<unsigned>(s_preview_images.size()));
            }
            ::ui::i18n::set_label_text_raw(state.route_preview_status_label, status);
            lv_obj_clear_flag(state.route_preview_status_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(state.route_preview_status_label, LV_ALIGN_TOP_RIGHT, -5, 5);
            lv_obj_move_foreground(state.route_preview_status_label);
        }
    }

    if (state.route_preview_progress &&
        lv_obj_is_valid(state.route_preview_progress))
    {
        if (s_preview_images.empty())
        {
            lv_obj_add_flag(state.route_preview_progress, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(state.route_preview_progress, 0, LV_ANIM_OFF);
            return;
        }
        lv_obj_clear_flag(state.route_preview_progress, LV_OBJ_FLAG_HIDDEN);
        int value = 0;
        if (route_preview_download_status_matches(download_status) && download_status.total > 0)
        {
            std::uint32_t current_percent = 0;
            if (download_status.busy &&
                download_status.current_total_bytes > 0)
            {
                const std::uint32_t current_bytes =
                    std::min(download_status.current_bytes, download_status.current_total_bytes);
                current_percent = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(
                        100U,
                        (static_cast<std::uint64_t>(current_bytes) * 100U) /
                            download_status.current_total_bytes));
            }
            if (download_status.total < s_preview_images.size())
            {
                const std::size_t completed = std::min<std::size_t>(
                    s_preview_images.size(),
                    saved_count + download_status.processed);
                const std::size_t scaled =
                    std::min<std::size_t>(
                        s_preview_images.size() * 100U,
                        completed * 100U + current_percent);
                value = static_cast<int>(scaled / s_preview_images.size());
            }
            else
            {
                const std::size_t completed = std::min<std::size_t>(
                    download_status.total,
                    download_status.processed);
                const std::size_t scaled =
                    std::min<std::size_t>(
                        download_status.total * 100U,
                        completed * 100U + current_percent);
                value = static_cast<int>(scaled / download_status.total);
            }
        }
        else
        {
            const std::size_t numerator =
                saved_count >= s_preview_images.size() &&
                        s_preview_image_cache_state_known
                    ? cached_count
                    : saved_count;
            value = static_cast<int>((numerator * 100U) / s_preview_images.size());
        }
        lv_bar_set_value(state.route_preview_progress, value, LV_ANIM_ON);
    }
}

lv_obj_t* create_preview_page_button(lv_obj_t* parent,
                                     const char* text,
                                     lv_event_cb_t cb,
                                     lv_coord_t width)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, action_menu_button_height());
    lv_obj_t* label = lv_label_create(btn);
    ::ui::i18n::set_label_text(label, text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(label);
    apply_action_button(btn, label);
    if (cb)
    {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    }
    bind_route_preview_key_handler(btn);
    if (lv_group_t* group = tracker_group())
    {
        lv_group_add_obj(group, btn);
    }
    return btn;
}

void preview_map_control(lv_event_t* event)
{
    const auto action = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    if (!action) s_preview_view.reset();
    else if (!s_preview_view.zoomBy(static_cast<int>(action))) return;
    refresh_route_preview_map();
}

void create_preview_map_controls()
{
    auto* host = g_tracker_state.route_preview_map_host;
    if (!host || !lv_obj_is_valid(host)) return;
    if (!s_preview_map_controls || !lv_obj_is_valid(s_preview_map_controls))
    {
        s_preview_map_controls = lv_obj_create(host);
        lv_obj_add_flag(s_preview_map_controls, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(s_preview_map_controls, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s_preview_map_controls, 122, 36);
        lv_obj_align(s_preview_map_controls, LV_ALIGN_TOP_LEFT, 4, 4);
        lv_obj_set_flex_flow(s_preview_map_controls, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_bg_opa(s_preview_map_controls, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_preview_map_controls, 0, 0);
        lv_obj_set_style_pad_all(s_preview_map_controls, 0, 0);
        lv_obj_set_style_pad_column(s_preview_map_controls, 4, 0);
        const char* labels[] = {"+", "-", "Fit"};
        for (int index = 0; index < 3; ++index)
        {
            auto* button = create_preview_page_button(s_preview_map_controls, labels[index], nullptr, 38);
            lv_obj_set_height(button, 36);
            lv_obj_set_style_pad_all(button, 2, 0);
            lv_obj_add_event_cb(button, preview_map_control, LV_EVENT_CLICKED,
                                reinterpret_cast<void*>(intptr_t(index == 0 ? 1 : index == 1 ? -1
                                                                                             : 0)));
        }
    }
    auto* zoom_in = lv_obj_get_child(s_preview_map_controls, 0);
    auto* zoom_out = lv_obj_get_child(s_preview_map_controls, 1);
    if (s_preview_view.zoom >= ::ui::widgets::map::kMaxZoom) lv_obj_add_state(zoom_in, LV_STATE_DISABLED);
    else lv_obj_clear_state(zoom_in, LV_STATE_DISABLED);
    if (s_preview_view.zoom <= ::ui::widgets::map::kMinZoom) lv_obj_add_state(zoom_out, LV_STATE_DISABLED);
    else lv_obj_clear_state(zoom_out, LV_STATE_DISABLED);
    lv_obj_move_foreground(s_preview_map_controls);
}

bool preview_selected_route_is_active()
{
    const auto& state = g_tracker_state;
    return !state.active_route.empty() &&
           !state.selected_route.empty() &&
           state.active_route == state.selected_route;
}

void update_route_preview_load_button()
{
    auto& state = g_tracker_state;
    if (!state.route_preview_load_label)
    {
        return;
    }
    ::ui::i18n::set_label_text(state.route_preview_load_label,
                               preview_selected_route_is_active() ? "Off" : "Load");
    lv_obj_center(state.route_preview_load_label);
    update_route_preview_buttons();
}

::ui::widgets::route_elevation_profile::Config route_preview_elevation_profile_config()
{
    ::ui::widgets::route_elevation_profile::Config config{};
    config.max_points = kPreviewMaxDrawPoints;
    return config;
}

::ui::widgets::route_elevation_profile::Metrics route_preview_elevation_metrics()
{
    ::ui::widgets::route_elevation_profile::Metrics metrics{};
    metrics.altitude_count = s_preview_metrics.altitude_count;
    metrics.min_altitude_m = s_preview_metrics.min_altitude_m;
    metrics.max_altitude_m = s_preview_metrics.max_altitude_m;
    metrics.ascent_m = s_preview_metrics.ascent_m;
    metrics.descent_m = s_preview_metrics.descent_m;
    metrics.distance_m = s_preview_metrics.distance_m;
    return metrics;
}

void ensure_route_preview_elevation_profile()
{
    auto& state = g_tracker_state;
    if (!state.route_preview_map_host ||
        !lv_obj_is_valid(state.route_preview_map_host))
    {
        return;
    }
    if (!s_preview_elevation_profile.panel ||
        !lv_obj_is_valid(s_preview_elevation_profile.panel))
    {
        ::ui::widgets::route_elevation_profile::create(
            state.route_preview_map_host,
            s_preview_elevation_profile,
            route_preview_elevation_profile_config());
    }
    state.route_preview_elevation_panel = s_preview_elevation_profile.panel;
}

void rebuild_preview_elevation_samples()
{
    if (!s_preview_elevation_samples.empty()) return; // full-stream distances already projected at load
    s_preview_elevation_samples.clear();
    if (!s_preview_elevation_samples.reserve(s_preview_route_points.size())) return;
    double distance_m = 0.0;
    const RoutePreviewPoint* previous = nullptr;
    for (const auto& point : s_preview_route_points)
    {
        if (previous && !point.break_before)
        {
            const double step = preview_distance_m(*previous, point);
            if (std::isfinite(step))
            {
                distance_m += step;
            }
        }
        previous = &point;
        ::ui::widgets::route_elevation_profile::Sample sample{};
        sample.altitude_m = point.altitude_m;
        sample.distance_m = distance_m;
        sample.has_altitude = point.has_altitude;
        sample.break_before = point.break_before;
        s_preview_elevation_samples.push_back(sample);
    }
}

void sync_route_preview_elevation_visibility()
{
    auto& state = g_tracker_state;
    if (!state.route_preview_page ||
        !state.route_preview_map_host ||
        !lv_obj_is_valid(state.route_preview_map_host))
    {
        return;
    }

    refresh_route_preview_map();
    ensure_route_preview_elevation_profile();
    if (!s_preview_elevation_profile.panel ||
        !lv_obj_is_valid(s_preview_elevation_profile.panel))
    {
        return;
    }

    if (!s_preview_elevation_visible || s_preview_metrics.altitude_count < 2)
    {
        ::ui::widgets::route_elevation_profile::set_hidden(s_preview_elevation_profile, true);
    }
    else
    {
        rebuild_preview_elevation_samples();
        (void)::ui::widgets::route_elevation_profile::update(
            s_preview_elevation_profile,
            route_preview_elevation_profile_config(),
            s_preview_elevation_samples.data(),
            s_preview_elevation_samples.size(),
            route_preview_elevation_metrics(),
            true,
            4);
    }
    lv_obj_update_layout(state.route_preview_page);
}

void toggle_route_preview_elevation()
{
    s_preview_elevation_visible = !s_preview_elevation_visible;
    s_preview_status_text = s_preview_elevation_visible ? "Profile shown" : "Profile hidden";
    sync_route_preview_elevation_visibility();
    update_route_preview_status();
}

void cycle_route_preview_map_layer()
{
    const auto layer_state = ::ui::widgets::map::current_layer_state();
    const uint8_t next_source = static_cast<uint8_t>((layer_state.map_source + 1U) % 3U);
    ::ui::widgets::map::LayerNotice notice{};
    (void)::ui::widgets::map::set_layer_map_source(next_source, &notice);
    if (notice.has_message)
    {
        s_preview_status_text = notice.message;
    }
    else
    {
        char text[40]{};
        std::snprintf(text,
                      sizeof(text),
                      "Base %s",
                      ::ui::i18n::tr(::ui::widgets::map::layer_map_source_label_key(next_source)));
        s_preview_status_text = text;
    }
    refresh_route_preview_map();
    update_route_preview_status();
}

void consume_route_preview_key_event(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    lv_event_stop_bubbling(e);
    lv_event_stop_processing(e);
}

void close_route_preview_help_modal()
{
    modal_close(g_tracker_state.route_preview_help_modal);
}

void on_route_preview_help_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE ||
        key == LV_KEY_ENTER || key == 'h' || key == 'H')
    {
        consume_route_preview_key_event(e);
        close_route_preview_help_modal();
        return;
    }
    if (key == LV_KEY_UP || key == 'w' || key == 'W')
    {
        lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
        if (target && lv_obj_is_valid(target))
        {
            lv_obj_scroll_by(target, 0, 18, LV_ANIM_OFF);
        }
        consume_route_preview_key_event(e);
        return;
    }
    if (key == LV_KEY_DOWN || key == 's' || key == 'S')
    {
        lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
        if (target && lv_obj_is_valid(target))
        {
            lv_obj_scroll_by(target, 0, -18, LV_ANIM_OFF);
        }
        consume_route_preview_key_event(e);
    }
}

void open_route_preview_help_modal()
{
    auto& state = g_tracker_state;
    if (state.route_preview_help_modal)
    {
        return;
    }

    modal_prepare_group();
    const auto frame = ::ui::components::dialog_frame::create(lv_screen_active(),
                                                              {"Preview Help", 304, 176, [](lv_event_t*)
                                                               { close_route_preview_help_modal(); },
                                                               on_route_preview_help_key});
    state.route_preview_help_modal = frame.overlay;
    lv_obj_t* win = frame.body;
    lv_obj_add_flag(win, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(win, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    auto add_keycap = [](lv_obj_t* parent, const char* text, lv_coord_t width)
    {
        lv_obj_t* keycap = lv_label_create(parent);
        lv_obj_set_size(keycap, width, 14);
        lv_obj_set_style_bg_color(keycap, lv_color_hex(0xF8E6C3), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(keycap, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(keycap, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(keycap, lv_color_hex(0x8A6E43), LV_PART_MAIN);
        lv_obj_set_style_radius(keycap, 3, LV_PART_MAIN);
        lv_obj_set_style_text_font(keycap, &lv_font_montserrat_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(keycap, lv_color_hex(0x25170D), LV_PART_MAIN);
        lv_obj_set_style_text_align(keycap, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(keycap, LV_LABEL_LONG_CLIP);
        lv_label_set_text(keycap, text ? text : "");
        return keycap;
    };

    auto add_help_row = [&](const char* primary,
                            const char* secondary,
                            const char* description)
    {
        lv_obj_t* row = lv_obj_create(win);
        lv_obj_set_size(row, LV_PCT(100), 15);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_column(row, 3, LV_PART_MAIN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* keys = lv_obj_create(row);
        lv_obj_set_size(keys, 76, 15);
        lv_obj_set_flex_flow(keys, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(keys,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(keys, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(keys, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(keys, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_column(keys, 2, LV_PART_MAIN);
        lv_obj_clear_flag(keys, LV_OBJ_FLAG_SCROLLABLE);

        if (secondary && secondary[0] != '\0')
        {
            const lv_coord_t secondary_width =
                std::strlen(secondary) > 4 ? 48 : (std::strlen(secondary) > 2 ? 34 : 22);
            add_keycap(keys, primary, std::strlen(primary) > 2 ? 34 : 22);
            add_keycap(keys, secondary, secondary_width);
        }
        else
        {
            add_keycap(keys, primary, 72);
        }

        lv_obj_t* text = lv_label_create(row);
        lv_obj_set_width(text, 0);
        lv_obj_set_flex_grow(text, 1);
        lv_obj_set_style_text_font(text, &lv_font_montserrat_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(text, lv_color_hex(0x3E2B18), LV_PART_MAIN);
        lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
        lv_label_set_text(text, description ? description : "");
    };

    add_help_row("WASD", nullptr, "Pan map (images hidden)");
    add_help_row("Q / -", nullptr, "Zoom out");
    add_help_row("E / +", nullptr, "Zoom in");
    add_help_row("F", nullptr, "Fit whole route");
    add_help_row("R", nullptr, "Load/off route");
    add_help_row("L", nullptr, "Change base layer");
    add_help_row("V", nullptr, "Show/hide profile");
    add_help_row("G", nullptr, "Download images");
    add_help_row("P", nullptr, "Show/hide images");
    add_help_row("W/S", nullptr, "Select image when shown");
    add_help_row("Enter", nullptr, "Open/close image");
    add_help_row("Pin", nullptr, "Select image point");
    add_help_row("W/S", nullptr, "Scroll this help");
    add_help_row("X/H", "Back", "Close help");

    lv_obj_move_foreground(state.route_preview_help_modal);
    lv_group_add_obj(state.modal_group, win);
    lv_group_add_obj(state.modal_group, frame.close);
    ::ui::components::dialog_frame::bind_close_shortcuts(frame, state.modal_group);
    lv_group_focus_obj(win);
}

void on_route_preview_load_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    if (route_preview_load_button_disabled())
    {
        if (route_preview_download_busy_for_current_route())
        {
            s_preview_status_text = "Download in progress";
        }
        else if (route_preview_all_image_caches_ready())
        {
            s_preview_status_text = "Images already saved";
        }
        else
        {
            s_preview_status_text = "Load unavailable";
        }
        update_route_preview_status();
        update_route_preview_buttons();
        return;
    }
    if (preview_selected_route_is_active())
    {
        on_route_unload_clicked(nullptr);
        s_preview_status_text = "Route off";
    }
    else if (state.active_route.empty())
    {
        on_route_load_clicked(nullptr);
        s_preview_status_text = "Route loaded";
    }
    else
    {
        s_preview_status_text = "Another route active";
    }
    update_route_preview_load_button();
    update_route_preview_status();
}

void close_route_preview_page()
{
    auto& state = g_tracker_state;
    if (state.route_preview_help_modal)
    {
        modal_close(state.route_preview_help_modal);
    }
    pause_route_preview_download_poll_timer();
    destroy_route_preview_image_strip(true);
    ::ui::widgets::map::destroy(s_preview_map_runtime);
    unbind_route_preview_key_handler(state.top_bar.back_btn);
    if (state.route_preview_page)
    {
        lv_obj_del(state.route_preview_page);
    }
    state.route_preview_page = nullptr;
    state.route_preview_map_host = nullptr;
    state.route_preview_elevation_panel = nullptr;
    state.route_preview_status_label = nullptr;
    state.route_preview_progress = nullptr;
    state.route_preview_download_btn = nullptr;
    state.route_preview_load_btn = nullptr;
    state.route_preview_load_label = nullptr;
    clear_preview_model();
    if (state.bottom_bar) lv_obj_clear_flag(state.bottom_bar, LV_OBJ_FLAG_HIDDEN);
    if (state.list_panel)
    {
        lv_obj_clear_flag(state.list_panel, LV_OBJ_FLAG_HIDDEN);
    }
    ::ui::widgets::top_bar_set_title(state.top_bar, ::ui::i18n::tr("Tracker"));
    update_route_status();
    tracker::ui::input::tracker_input_on_ui_refreshed();
    focus_main_panel();
}

void on_route_preview_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    auto& state = g_tracker_state;
    if (!state.route_preview_page || !lv_obj_is_valid(state.route_preview_page))
    {
        return;
    }
    const uint32_t key = lv_event_get_key(e);
    if (::ui::widgets::route_image_strip::handle_key(s_preview_image_strip, key))
    {
        consume_route_preview_key_event(e);
        return;
    }
    if (::ui::widgets::route_image_strip::is_visible(s_preview_image_strip) &&
        (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE))
    {
        s_preview_image_strip_visible = false;
        sync_route_preview_image_strip();
        s_preview_status_text = "Images hidden";
        update_route_preview_status();
        consume_route_preview_key_event(e);
        return;
    }
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        consume_route_preview_key_event(e);
        close_route_preview_page();
        return;
    }
    if (key == '+' || key == '=' || key == '-' || key == '_' ||
        key == 'q' || key == 'Q' || key == 'e' || key == 'E')
    {
        if (s_preview_view.zoomBy(key == '+' || key == '=' || key == 'e' || key == 'E' ? 1 : -1)) refresh_route_preview_map();
        consume_route_preview_key_event(e);
        return;
    }
    if (key == 'f' || key == 'F')
    {
        s_preview_view.reset();
        refresh_route_preview_map();
        consume_route_preview_key_event(e);
        return;
    }
    if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT || key == LV_KEY_UP || key == LV_KEY_DOWN ||
        key == 'w' || key == 'W' || key == 'a' || key == 'A' ||
        key == 's' || key == 'S' || key == 'd' || key == 'D')
    {
        const bool left = key == LV_KEY_LEFT || key == 'a' || key == 'A';
        const bool right = key == LV_KEY_RIGHT || key == 'd' || key == 'D';
        const bool up = key == LV_KEY_UP || key == 'w' || key == 'W';
        const bool down = key == LV_KEY_DOWN || key == 's' || key == 'S';
        s_preview_view.pan(left ? 40 : right ? -40
                                             : 0,
                           up ? 40 : down ? -40
                                          : 0);
        refresh_route_preview_map();
        consume_route_preview_key_event(e);
        return;
    }
    if (key == 'g' || key == 'G')
    {
        consume_route_preview_key_event(e);
        on_route_preview_download_clicked(nullptr);
        return;
    }
    if (key == 'r' || key == 'R')
    {
        consume_route_preview_key_event(e);
        on_route_preview_load_clicked(nullptr);
        return;
    }
    if (key == 'l' || key == 'L')
    {
        consume_route_preview_key_event(e);
        cycle_route_preview_map_layer();
        return;
    }
    if (key == 'v' || key == 'V')
    {
        consume_route_preview_key_event(e);
        toggle_route_preview_elevation();
        return;
    }
    if (key == 'p' || key == 'P')
    {
        toggle_route_preview_image_strip();
        consume_route_preview_key_event(e);
        return;
    }
    if (key == 'h' || key == 'H' || key == '?')
    {
        consume_route_preview_key_event(e);
        open_route_preview_help_modal();
        return;
    }
}

void on_route_preview_download_clicked(lv_event_t*)
{
    refresh_preview_image_storage_state(true);
    const std::size_t initial_saved_count = preview_saved_image_count();
    std::printf("[RoutePreview][down] clicked asset=%s images=%u saved=%u\n",
                s_preview_asset_id.c_str(),
                static_cast<unsigned>(s_preview_images.size()),
                static_cast<unsigned>(initial_saved_count));
    if (s_preview_images.empty())
    {
        s_preview_status_text = "No route images";
        s_preview_download_state = RoutePreviewDownloadState::Failed;
        std::printf("[RoutePreview][down] blocked reason=no_images asset=%s\n",
                    s_preview_asset_id.c_str());
        show_route_preview_notice("No route images");
        update_route_preview_status();
        update_route_preview_buttons();
        return;
    }

    const std::size_t total = s_preview_images.size();
    std::size_t saved_count = initial_saved_count;
    char detail[96]{};

    if (route_preview_download_busy_for_current_route())
    {
        std::printf("[RoutePreview][down] already_running asset=%s saved=%u/%u\n",
                    s_preview_asset_id.c_str(),
                    static_cast<unsigned>(saved_count),
                    static_cast<unsigned>(total));
        show_route_preview_notice("Download in progress");
        sync_route_preview_download_status();
        ensure_route_preview_download_poll_timer();
        return;
    }

    if (saved_count >= total)
    {
        s_preview_download_state = RoutePreviewDownloadState::Done;
        std::snprintf(detail,
                      sizeof(detail),
                      "Images ready %u/%u",
                      static_cast<unsigned>(total),
                      static_cast<unsigned>(total));
        s_preview_status_text = detail;
        std::printf("[RoutePreview][down] blocked reason=already_ready asset=%s total=%u\n",
                    s_preview_asset_id.c_str(),
                    static_cast<unsigned>(total));
        show_route_preview_notice("Images already saved");
        update_route_preview_status();
        update_route_preview_buttons();
        return;
    }

    if (track_sharing::isInstalledPackageRouteAtRoot(route_path_for_name(s_preview_route_name), platform::ui::route_storage::route_dir()))
    {
        ensure_route_preview_image_cache_build(false);
        return;
    }
    const auto network = platform::ui::network::status();
    if (!network.supported || !network.connected)
    {
        s_preview_status_text = "Network connection required";
        s_preview_download_state = RoutePreviewDownloadState::Failed;
        std::printf("[RoutePreview][down] blocked reason=network_required supported=%u connected=%u\n",
                    network.supported ? 1U : 0U, network.connected ? 1U : 0U);
        show_route_preview_notice("Network connection required");
        update_route_preview_status();
        update_route_preview_buttons();
        return;
    }

    std::string asset_dir;
    if (!platform::ui::route_storage::ensure_route_asset_dir(s_preview_asset_id, asset_dir))
    {
        s_preview_status_text = "Create image dir failed";
        s_preview_download_state = RoutePreviewDownloadState::Failed;
        std::printf("[RoutePreview][down] blocked reason=asset_dir_failed asset=%s\n",
                    s_preview_asset_id.c_str());
        show_route_preview_notice("Create image dir failed");
        update_route_preview_status();
        update_route_preview_buttons();
        return;
    }

    saved_count = preview_saved_image_count();
    s_preview_download_state = RoutePreviewDownloadState::Downloading;
    std::snprintf(detail,
                  sizeof(detail),
                  "Starting  saved %u/%u",
                  static_cast<unsigned>(saved_count),
                  static_cast<unsigned>(total));
    s_preview_status_text = detail;
    std::vector<platform::ui::route_storage::RouteImageDownloadItem> items;
    items.reserve(total > saved_count ? total - saved_count : 0);
    for (const auto& image : s_preview_images)
    {
        if (image.downloaded)
        {
            continue;
        }
        platform::ui::route_storage::RouteImageDownloadItem item{};
        item.url = image.url;
        item.output_path = image.local_path;
        item.preview_path = image.preview_path;
        item.view_path = image.view_path;
        items.push_back(std::move(item));
    }

    if (items.empty())
    {
        s_preview_download_state = RoutePreviewDownloadState::Done;
        s_preview_status_text = "Images already saved";
        update_route_preview_status();
        update_route_preview_buttons();
        return;
    }

    std::printf("[RoutePreview][down] starting asset=%s saved=%u/%u queued=%u\n",
                s_preview_asset_id.c_str(),
                static_cast<unsigned>(saved_count),
                static_cast<unsigned>(total),
                static_cast<unsigned>(items.size()));
    update_route_preview_status();
    update_route_preview_buttons();

    std::string error;
    const std::size_t queued_count = items.size();
    if (!platform::ui::route_storage::start_route_image_download(
            s_preview_asset_id,
            std::move(items),
            error,
            platform::ui::route_storage::RouteImageTaskPresentation::UserVisible))
    {
        s_preview_download_state = RoutePreviewDownloadState::Failed;
        s_preview_status_text = error.empty() ? "Start download failed" : error;
        std::printf("[RoutePreview][down] start_failed asset=%s error=%s\n",
                    s_preview_asset_id.c_str(),
                    s_preview_status_text.c_str());
        show_route_preview_notice(s_preview_status_text.c_str(), 2200);
        update_route_preview_status();
        update_route_preview_buttons();
        return;
    }

    std::printf("[RoutePreview][down] task_started asset=%s total=%u\n",
                s_preview_asset_id.c_str(),
                static_cast<unsigned>(queued_count));
    show_route_preview_notice("Download started");
    s_preview_download_refresh_map_on_finish = true;
    ensure_route_preview_download_poll_timer();
    ::ui::widgets::route_image_operation::refresh();
    sync_route_preview_download_status();
    update_route_preview_status();
    update_route_preview_buttons();
}

void render_route_preview_page()
{
    auto& state = g_tracker_state;
    if (!state.route_preview_page)
    {
        return;
    }
    destroy_route_preview_image_strip(false);
    ::ui::widgets::map::destroy(s_preview_map_runtime);
    s_preview_map_controls = nullptr;
    lv_obj_clean(state.route_preview_page);
    ::ui::widgets::route_elevation_profile::reset(s_preview_elevation_profile);
    state.route_preview_download_btn = nullptr;
    state.route_preview_load_btn = nullptr;
    state.route_preview_load_label = nullptr;
    bind_route_preview_key_handler(state.top_bar.back_btn);
    lv_group_t* group = tracker_group();
    if (group)
    {
        lv_group_remove_all_objs(group);
        if (state.top_bar.back_btn)
        {
            lv_group_add_obj(group, state.top_bar.back_btn);
        }
    }

    state.route_preview_map_host = lv_obj_create(state.route_preview_page);
    set_plain_panel(state.route_preview_map_host, 0xD8C090);
    lv_obj_set_width(state.route_preview_map_host, LV_PCT(100));
    lv_obj_set_height(state.route_preview_map_host, 0);
    lv_obj_set_flex_grow(state.route_preview_map_host, 1);
    lv_obj_add_flag(state.route_preview_map_host, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(state.route_preview_map_host, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_border_width(state.route_preview_map_host, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(state.route_preview_map_host, lv_color_hex(0xD2A868), LV_PART_MAIN);
    lv_obj_set_style_border_color(state.route_preview_map_host, lv_color_hex(kPanelBtnFocused), LV_PART_MAIN | LV_STATE_FOCUSED);
    bind_route_preview_key_handler(state.route_preview_map_host);
    if (group)
    {
        lv_group_add_obj(group, state.route_preview_map_host);
    }

    state.route_preview_elevation_panel = nullptr;

    state.route_preview_status_label = lv_label_create(state.route_preview_map_host);
    lv_obj_set_size(state.route_preview_status_label, 52, 18);
    lv_obj_add_flag(state.route_preview_status_label, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(state.route_preview_status_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(state.route_preview_status_label, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_long_mode(state.route_preview_status_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_bg_color(state.route_preview_status_label, lv_color_hex(0x1C1812), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(state.route_preview_status_label, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(state.route_preview_status_label, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(state.route_preview_status_label, lv_color_hex(0xFFF3DF), LV_PART_MAIN);
    lv_obj_set_style_radius(state.route_preview_status_label, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_top(state.route_preview_status_label, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(state.route_preview_status_label, 1, LV_PART_MAIN);
    lv_obj_set_style_text_font(
        state.route_preview_status_label, ::ui::fonts::localized_font(&lv_font_montserrat_12), 0);
    lv_obj_set_style_text_color(state.route_preview_status_label, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_text_align(state.route_preview_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(state.route_preview_status_label, LV_ALIGN_TOP_RIGHT, -5, 5);

    state.route_preview_progress = lv_bar_create(state.route_preview_page);
    lv_obj_set_size(state.route_preview_progress, LV_PCT(100), 7);
    lv_bar_set_range(state.route_preview_progress, 0, 100);
    lv_bar_set_value(state.route_preview_progress, 0, LV_ANIM_OFF);

    lv_obj_t* row = lv_obj_create(state.route_preview_page);
    lv_obj_set_size(row, LV_PCT(100), action_menu_button_height());
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, kPreviewButtonGap, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_update_layout(state.route_preview_page);
    const lv_coord_t page_w =
        std::max<lv_coord_t>(120, lv_obj_get_content_width(state.route_preview_page));
    const lv_coord_t base_button_w =
        page_profile().dense ? kPreviewButtonWidthDense : kPreviewButtonWidth;
    const lv_coord_t max_button_w =
        std::max<lv_coord_t>(46, (page_w - (kPreviewButtonGap * 3)) / 2);
    const lv_coord_t button_w = std::min<lv_coord_t>(base_button_w, max_button_w);
    lv_obj_t* download_btn = create_preview_page_button(row, "Down", on_route_preview_download_clicked, button_w);
    lv_obj_t* load_btn = create_preview_page_button(row, "Load", on_route_preview_load_clicked, button_w);
    state.route_preview_download_btn = download_btn;
    state.route_preview_load_btn = load_btn;
    state.route_preview_load_label = lv_obj_get_child(load_btn, 0);
    update_route_preview_load_button();

    lv_obj_t* help_hint = lv_label_create(state.route_preview_page);
    lv_obj_set_size(help_hint, 20, 16);
    lv_obj_add_flag(help_hint, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(help_hint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(help_hint, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_bg_color(help_hint, lv_color_hex(0xF8E6C3), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(help_hint, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(help_hint, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(help_hint, lv_color_hex(0x8A6E43), LV_PART_MAIN);
    lv_obj_set_style_radius(help_hint, 3, LV_PART_MAIN);
    lv_obj_set_style_text_font(help_hint, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(help_hint, lv_color_hex(0x25170D), LV_PART_MAIN);
    lv_obj_set_style_text_align(help_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(help_hint, LV_LABEL_LONG_CLIP);
    lv_label_set_text(help_hint, "H");
    lv_obj_align(help_hint, LV_ALIGN_BOTTOM_LEFT, 2, -1);
    lv_obj_add_event_cb(
        help_hint,
        [](lv_event_t* e)
        {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED)
            {
                open_route_preview_help_modal();
            }
        },
        LV_EVENT_CLICKED,
        nullptr);
    lv_group_remove_obj(help_hint);

    update_route_preview_buttons();
    lv_obj_t* focus_obj = s_preview_images.empty() ? load_btn : download_btn;
    if (route_preview_download_button_disabled())
    {
        focus_obj = load_btn;
    }
    if (route_preview_load_button_disabled() && focus_obj == load_btn)
    {
        focus_obj = state.route_preview_map_host;
    }
    if (group)
    {
        lv_group_focus_obj(focus_obj);
    }

    lv_obj_update_layout(state.route_preview_page);
    sync_route_preview_elevation_visibility();
    if (route_preview_download_busy_for_current_route())
    {
        sync_route_preview_download_status();
    }
    else
    {
        sync_route_preview_map_loader_pause();
    }
    update_route_preview_status();
}

void open_route_preview_page()
{
    auto& state = g_tracker_state;
    if (is_any_modal_open() || state.route_preview_page)
    {
        return;
    }
    if (!can_preview_selected_route())
    {
        if (state.status_label)
        {
            ::ui::i18n::set_label_text(state.status_label, "Select a route");
        }
        return;
    }
    if (!load_route_preview_model(state.selected_route))
    {
        if (state.status_label)
        {
            if (!s_preview_status_text.empty()) ::ui::i18n::set_label_text_raw(state.status_label, s_preview_status_text.c_str());
            else ::ui::i18n::set_label_text(state.status_label, "Preview failed");
        }
        return;
    }

    if (state.filter_panel)
    {
        lv_obj_add_flag(state.filter_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (state.list_panel)
    {
        lv_obj_add_flag(state.list_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (state.bottom_bar) lv_obj_add_flag(state.bottom_bar, LV_OBJ_FLAG_HIDDEN);
    ::ui::widgets::top_bar_set_title(state.top_bar, ::ui::i18n::tr("Route Preview"));

    state.route_preview_page = lv_obj_create(state.content);
    if (!state.route_preview_page)
    {
        clear_preview_model();
        return;
    }
    bind_route_preview_key_handler(state.route_preview_page);
    lv_obj_set_width(state.route_preview_page, LV_PCT(100));
    lv_obj_set_height(state.route_preview_page, LV_PCT(100));
    lv_obj_set_flex_grow(state.route_preview_page, 1);
    lv_obj_set_flex_flow(state.route_preview_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(state.route_preview_page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(state.route_preview_page, page_profile().dense ? 2 : 3, LV_PART_MAIN);
    lv_obj_set_style_pad_row(state.route_preview_page, page_profile().dense ? 2 : 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(state.route_preview_page, lv_color_hex(0xFAF0D8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(state.route_preview_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(state.route_preview_page, 0, LV_PART_MAIN);
    lv_obj_clear_flag(state.route_preview_page, LV_OBJ_FLAG_SCROLLABLE);
    render_route_preview_page();
    if (route_preview_download_busy_for_current_route())
    {
        ensure_route_preview_download_poll_timer();
    }
}

lv_group_t* tracker_group()
{
    if (lv_group_t* group = tracker::ui::input::tracker_input_get_group())
    {
        return group;
    }
    return ::app_g;
}

void clear_list_items()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return;
    }
    while (lv_obj_get_child_count(state.list_container) > 0)
    {
        lv_obj_del(lv_obj_get_child(state.list_container, 0));
    }
}

lv_obj_t* create_list_item_button(const std::string& text, intptr_t user_data, bool checked, bool disabled)
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return nullptr;
    }

    lv_obj_t* btn = lv_btn_create(state.list_container);
    lv_obj_set_size(btn, LV_PCT(100), list_item_height());
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    apply_list_button(btn);

    lv_obj_t* label = lv_label_create(btn);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, page_profile().dense ? 6 : 10, 0);
    lv_obj_add_style(label, &s_btn_label, LV_PART_MAIN);
    lv_obj_set_width(label, LV_PCT(100));
    ::ui::i18n::set_content_label_text_raw(label, text.c_str());
    ::ui::i18n::log_direct_text_route("tracker_list_item", label, text.c_str());
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

    lv_obj_set_user_data(btn, reinterpret_cast<void*>(user_data));
    if (checked)
    {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(btn, LV_STATE_CHECKED);
    }
    if (disabled)
    {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_clear_state(btn, LV_STATE_DISABLED);
    }

    lv_obj_add_event_cb(btn, on_list_item_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btn, on_list_item_focused, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(btn, on_list_item_defocused, LV_EVENT_DEFOCUSED, nullptr);
    return btn;
}

void create_empty_state(const std::string& text)
{
    lv_obj_t* label = lv_label_create(g_tracker_state.list_container);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    ::ui::i18n::set_content_label_text_raw(label, text.c_str());
    lv_obj_set_style_text_color(label, lv_color_hex(kPanelTextMuted), 0);
}

void sync_list_item_checked_states()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return;
    }

    const int selected_idx =
        (state.mode == TrackerPageState::Mode::Record) ? state.selected_record_idx : state.selected_route_idx;
    const uint32_t child_count = lv_obj_get_child_count(state.list_container);
    for (uint32_t index = 0; index < child_count; ++index)
    {
        lv_obj_t* btn = lv_obj_get_child(state.list_container, index);
        if (!btn)
        {
            continue;
        }
        const intptr_t raw = reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn));
        const bool checked = raw >= static_cast<intptr_t>(kListUserDataOffset) &&
                             static_cast<int>(raw - static_cast<intptr_t>(kListUserDataOffset)) == selected_idx;
        if (checked)
        {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_clear_state(btn, LV_STATE_CHECKED);
        }
    }
}

lv_obj_t* first_visible_list_item()
{
    auto& state = g_tracker_state;
    if (!state.list_container)
    {
        return state.start_stop_btn;
    }

    const uint32_t child_count = lv_obj_get_child_count(state.list_container);
    for (uint32_t index = 0; index < child_count; ++index)
    {
        lv_obj_t* btn = lv_obj_get_child(state.list_container, index);
        if (btn && lv_obj_has_state(btn, LV_STATE_CHECKED) &&
            !lv_obj_has_flag(btn, LV_OBJ_FLAG_HIDDEN) &&
            !lv_obj_has_state(btn, LV_STATE_DISABLED))
        {
            return btn;
        }
    }
    for (uint32_t index = 0; index < child_count; ++index)
    {
        lv_obj_t* btn = lv_obj_get_child(state.list_container, index);
        if (btn && !lv_obj_has_flag(btn, LV_OBJ_FLAG_HIDDEN) &&
            !lv_obj_has_state(btn, LV_STATE_DISABLED))
        {
            return btn;
        }
    }
    return state.start_stop_btn;
}

void focus_mode_panel()
{
    tracker::ui::input::tracker_focus_to_filter();
}

void focus_main_panel()
{
    tracker::ui::input::tracker_focus_to_list();
}

void set_mode(TrackerPageState::Mode mode)
{
    auto& state = g_tracker_state;
    const bool change_navigation = state.mode != mode && (mode == TrackerPageState::Mode::Navigation || state.mode == TrackerPageState::Mode::Navigation);
    if (change_navigation)
    {
        state.selected_route.clear();
        state.selected_route_idx = -1;
    }
    state.mode = mode;
    if (state.status_label)
    {
        if (mode == TrackerPageState::Mode::Record) lv_obj_clear_flag(state.status_label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(state.status_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (change_navigation || mode == TrackerPageState::Mode::Navigation) refresh_route_list();
    update_mode_buttons();
    if (state.bottom_bar)
    {
        lv_obj_clear_flag(state.bottom_bar, LV_OBJ_FLAG_HIDDEN);
    }

    if (mode == TrackerPageState::Mode::Record)
    {
        update_record_status();
        update_start_stop_button();
        update_record_page();
    }
    else
    {
        update_start_stop_button();
        update_route_status();
        update_route_page();
    }
    if (state.focus_col == TrackerPageState::FocusColumn::Main)
    {
        tracker::ui::input::tracker_input_on_ui_refreshed();
    }
}

void update_record_status()
{
    auto& state = g_tracker_state;
    if (!state.status_label)
    {
        return;
    }
    const bool recording = platform::ui::tracker::is_recording();
    if (state.mode == TrackerPageState::Mode::Record)
    {
        ::ui::i18n::set_label_text(state.status_label, recording ? "Recording" : "Not recording");
        lv_obj_set_style_text_color(state.status_label,
                                    lv_color_hex(recording ? 0xC53D32 : kPanelTextMuted), 0);
    }
}

void update_start_stop_button()
{
    auto& state = g_tracker_state;
    const bool recording = platform::ui::tracker::is_recording();
    if (!state.start_stop_btn) return;
    if (state.mode == TrackerPageState::Mode::Record && recording) lv_obj_clear_flag(state.start_stop_btn, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(state.start_stop_btn, LV_OBJ_FLAG_HIDDEN);
}

size_t utf8_count_chars(const std::string& text)
{
    size_t count = 0;
    const size_t len = text.length();
    size_t i = 0;
    while (i < len)
    {
        uint8_t c = static_cast<uint8_t>(text[i]);
        size_t advance = 1;
        if ((c & 0x80) == 0x00)
        {
            advance = 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            advance = 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            advance = 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            advance = 4;
        }
        i += advance;
        ++count;
    }
    return count;
}

std::string utf8_truncate(const std::string& text, size_t max_chars)
{
    if (max_chars == 0)
    {
        return std::string("...");
    }
    size_t count = 0;
    const size_t len = text.length();
    size_t i = 0;
    while (i < len)
    {
        uint8_t c = static_cast<uint8_t>(text[i]);
        size_t advance = 1;
        if ((c & 0x80) == 0x00)
        {
            advance = 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            advance = 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            advance = 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            advance = 4;
        }
        if (count + 1 > max_chars)
        {
            break;
        }
        i += advance;
        ++count;
    }
    if (i >= len)
    {
        return text;
    }
    std::string out = text.substr(0, i);
    out += "...";
    return out;
}

std::string format_list_name(const std::string& name)
{
    const size_t max_chars = page_profile().dense ? 18U : 20U;
    if (utf8_count_chars(name) <= max_chars)
    {
        return name;
    }
    return utf8_truncate(name, max_chars);
}

void update_item_card();

void update_card_location()
{
    auto& state = g_tracker_state;
    if (!state.item_card || !state.card_location_model || is_any_modal_open()) return;
    auto& location = *state.card_location_model;
    if (!location.valid && state.mode == TrackerPageState::Mode::Record && platform::ui::tracker::is_recording())
    {
        double latitude = 0, longitude = 0;
        if (::ui::support::readCardFileLocation(location.file.c_str(), latitude, longitude))
            ::ui::support::updateCardLocation(state.card_location_model, true, latitude, longitude, state.card_location, state.card_distance);
    }
    ::ui::support::updateCardLocation(state.card_location_model, location.valid, location.latitude, location.longitude,
                                      state.card_location, state.card_distance);
}

void step_item_card(int direction)
{
    auto& state = g_tracker_state;
    if (is_any_modal_open()) return;
    const bool recording = state.mode == TrackerPageState::Mode::Record;
    const auto& names = recording ? s_record_names : s_route_names;
    auto& index = recording ? state.selected_record_idx : state.selected_route_idx;
    auto& selected = recording ? state.selected_record : state.selected_route;
    const int next = index + direction;
    if (next < 0 || next >= static_cast<int>(names.size())) return;
    index = next;
    selected = names[static_cast<size_t>(next)];
    update_item_card();
}

void update_item_card()
{
    auto& state = g_tracker_state;
    if (!state.list_container || is_any_modal_open()) return;
    if (!state.item_card)
    {
        // A fixed set of widgets serves every file. Only filenames are indexed;
        // KML, elevation and images remain in the existing on-demand preview.
        lv_obj_clear_flag(state.list_container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(state.list_container, 0, 0);
        const auto view = item_card_view::create(state.list_container,
                                                 ::ui::page_profile::resolve_body_font(), ::ui::page_profile::resolve_caption_font(),
                                                 ::ui::page_profile::resolve_title_font(),
                                                 page_profile().compact_touch_keyboard ? 36 : page_profile().control_button_height, page_profile().dense);
        auto* card = state.item_card = view.root;
        state.card_name = view.name;
        state.card_distance = view.distance;
        state.card_location = view.region;
        state.card_format = view.format;
        state.card_previous = view.previous;
        state.card_counter = view.counter;
        state.card_next = view.next;
        state.card_actions = view.actions;
        ::ui::i18n::set_label_text(lv_obj_get_child(view.actions, 0), "Actions");
        state.card_timer = lv_timer_create([](lv_timer_t*)
                                           { update_card_location(); },
                                           500, nullptr);
        lv_obj_add_event_cb(card, on_list_item_clicked, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(
            card, [](lv_event_t*)
            {
                                auto* input = lv_indev_get_act();
                                if (!input) return;
                                const auto direction = lv_indev_get_gesture_dir(input);
                                if (direction == LV_DIR_TOP || direction == LV_DIR_LEFT) step_item_card(1);
                                else if (direction == LV_DIR_BOTTOM || direction == LV_DIR_RIGHT) step_item_card(-1); },
            LV_EVENT_GESTURE, nullptr);
        for (auto* button : {state.card_previous, state.card_next})
        {
            const int direction = button == state.card_previous ? -1 : 1;
            lv_obj_add_event_cb(
                button, [](lv_event_t* event)
                { step_item_card(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)))); },
                LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(direction)));
        }
        lv_obj_add_event_cb(
            state.card_actions, [](lv_event_t*)
            { open_action_menu_modal(); },
            LV_EVENT_CLICKED, nullptr);
    }
    const bool recording = state.mode == TrackerPageState::Mode::Record;
    const auto& names = recording ? s_record_names : s_route_names;
    auto& index = recording ? state.selected_record_idx : state.selected_route_idx;
    auto& selected = recording ? state.selected_record : state.selected_route;
    const auto found = std::find(names.begin(), names.end(), selected);
    index = found != names.end() ? static_cast<int>(found - names.begin()) : names.empty() ? -1
                                                                                           : 0;
    if (index < 0)
    {
        selected.clear();
        if (state.card_location_model) state.card_location_model->file.clear();
        lv_obj_set_user_data(state.item_card, nullptr);
        lv_obj_add_state(state.item_card, LV_STATE_DISABLED);
        ::ui::i18n::set_content_label_text_raw(state.card_name, (recording ? s_record_empty_text : s_route_empty_text).c_str());
        ::ui::i18n::set_label_text(state.card_format, recording ? "Tracks" : "Routes");
        ::ui::support::updateCardLocation(state.card_location_model, false, 0, 0, state.card_location, state.card_distance);
    }
    else
    {
        selected = names[static_cast<size_t>(index)];
        lv_obj_set_user_data(state.item_card, reinterpret_cast<void*>(static_cast<intptr_t>(index) + kListUserDataOffset));
        lv_obj_clear_state(state.item_card, LV_STATE_DISABLED);
        const auto extension = selected.find_last_of('.');
        std::string format = extension == std::string::npos ? "" : selected.substr(extension + 1);
        std::transform(format.begin(), format.end(), format.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::toupper(ch)); });
        const char* status = ::ui::i18n::tr(recording ? "Tracks" : state.mode == TrackerPageState::Mode::Navigation ? "Navigation"
                                                                                                                    : "Routes");
        if (recording && platform::ui::tracker::is_recording())
        {
            std::string current;
            if (platform::ui::tracker::current_path(current) && path_basename(current) == selected) status = ::ui::i18n::tr("Recording");
        }
        if (!recording && state.active_route == selected)
            ::ui::i18n::set_content_label_text_raw(state.card_format, ::ui::i18n::format("Active: %s", format.c_str()).c_str());
        else lv_label_set_text_fmt(state.card_format, "%s | %s", format.c_str(), status);
        const auto path = recording ? std::string(platform::ui::tracker::track_dir()) + "/" + selected : route_path_for_name(selected);
        if (!state.card_location_model) state.card_location_model = ::platform::memory::createPsram<::ui::support::CardLocation>();
        if (state.card_location_model && state.card_location_model->file != path)
        {
            state.card_location_model->file = path;
            double latitude = 0, longitude = 0;
            std::string title;
            const bool has_position = ::ui::support::readCardFileLocation(path.c_str(), latitude, longitude, &title);
            if (title.empty()) title = ::ui::support::cardFileTitleFallback(selected, ::ui::i18n::tr("Route"));
            ::ui::i18n::set_content_label_text_raw(state.card_name, title.c_str());
            ::ui::support::updateCardLocation(state.card_location_model, has_position, latitude, longitude, state.card_location, state.card_distance);
        }
        else if (!state.card_location_model)
        {
            ::ui::i18n::set_content_label_text_raw(state.card_name, ::ui::support::cardFileTitleFallback(selected, ::ui::i18n::tr("Route")).c_str());
            ::ui::support::updateCardLocation(state.card_location_model, false, 0, 0, state.card_location, state.card_distance);
        }
        else update_card_location();
    }
    lv_label_set_text_fmt(state.card_counter, "%u / %u", index < 0 ? 0 : static_cast<unsigned>(index + 1), static_cast<unsigned>(names.size()));
    if (index <= 0) lv_obj_add_state(state.card_previous, LV_STATE_DISABLED);
    else lv_obj_clear_state(state.card_previous, LV_STATE_DISABLED);
    if (index < 0 || static_cast<size_t>(index + 1) >= names.size()) lv_obj_add_state(state.card_next, LV_STATE_DISABLED);
    else lv_obj_clear_state(state.card_next, LV_STATE_DISABLED);
    if (index < 0) lv_obj_add_state(state.card_actions, LV_STATE_DISABLED);
    else lv_obj_clear_state(state.card_actions, LV_STATE_DISABLED);
    if (!recording) update_route_status();
    if (state.focus_col == TrackerPageState::FocusColumn::Main && !is_any_modal_open())
    {
        tracker::ui::input::tracker_input_on_ui_refreshed();
    }
}

void update_record_page()
{
    update_item_card();
}

void refresh_record_list()
{
    auto& state = g_tracker_state;
    if (!state.list_container || is_any_modal_open())
    {
        return;
    }
    s_record_names.clear();
    s_record_empty_text = ::ui::i18n::tr("No tracks yet");

    if (!platform::ui::device::sd_ready())
    {
        s_record_empty_text = ::ui::i18n::tr("No SD Card");
        if (state.mode == TrackerPageState::Mode::Record)
        {
            update_record_page();
        }
        return;
    }

    constexpr size_t kMaxTracks = 32;
    platform::ui::tracker::list_tracks(s_record_names, kMaxTracks);
    if (state.mode == TrackerPageState::Mode::Record)
    {
        update_record_page();
    }
}

void deferred_record_list_refresh_cb(lv_timer_t* timer)
{
    if (timer)
    {
        lv_timer_del(timer);
    }
    if (timer == s_record_list_refresh_timer)
    {
        s_record_list_refresh_timer = nullptr;
    }

    auto& state = g_tracker_state;
    if (!state.root || !lv_obj_is_valid(state.root))
    {
        return;
    }
    refresh_record_list();
}

void cancel_deferred_record_list_refresh()
{
    if (s_record_list_refresh_timer)
    {
        lv_timer_del(s_record_list_refresh_timer);
        s_record_list_refresh_timer = nullptr;
    }
}

void schedule_deferred_record_list_refresh(uint32_t delay_ms)
{
    cancel_deferred_record_list_refresh();
    s_record_list_refresh_timer = lv_timer_create(deferred_record_list_refresh_cb, delay_ms, nullptr);
    if (s_record_list_refresh_timer)
    {
        lv_timer_set_repeat_count(s_record_list_refresh_timer, 1);
    }
}

void update_route_status()
{
    auto& state = g_tracker_state;
    if (!state.status_label)
    {
        return;
    }
    lv_obj_set_style_text_color(state.status_label, lv_color_hex(kPanelTextMuted), 0);
    lv_obj_set_style_text_font(
        state.status_label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    if (state.mode != TrackerPageState::Mode::Record)
    {
        if (!state.active_route.empty())
        {
            const std::string text = ::ui::i18n::format("Active: %s", state.active_route.c_str());
            ::ui::i18n::set_content_label_text_raw(state.status_label, text.c_str());
        }
        else if (!state.selected_route.empty())
        {
            const std::string text = ::ui::i18n::format("Selected: %s", state.selected_route.c_str());
            ::ui::i18n::set_content_label_text_raw(state.status_label, text.c_str());
        }
        else
        {
            ::ui::i18n::set_label_text(state.status_label, "No route selected");
        }
    }
}

void update_route_page()
{
    update_item_card();
}

void refresh_route_list()
{
    auto& state = g_tracker_state;
    if (!state.list_container || is_any_modal_open())
    {
        return;
    }
    s_route_names.clear();
    s_route_empty_text = ::ui::i18n::tr(state.mode == TrackerPageState::Mode::Navigation ? "No saved navigation" : "No KML routes");

    if (!platform::ui::device::sd_ready())
    {
        s_route_empty_text = ::ui::i18n::tr("No SD Card");
        if (state.mode != TrackerPageState::Mode::Record)
        {
            update_route_page();
        }
        return;
    }

    const bool route_dir_ready = state.mode == TrackerPageState::Mode::Navigation ? platform::ui::navigation::list(s_route_names, 64) : platform::ui::route_storage::list_routes(s_route_names, 64);
    if (!route_dir_ready)
    {
        s_route_empty_text = ::ui::i18n::tr("No routes folder");
        if (state.mode != TrackerPageState::Mode::Record)
        {
            update_route_page();
        }
        return;
    }

    if (s_route_names.empty())
    {
        if (state.mode != TrackerPageState::Mode::Record)
        {
            update_route_page();
        }
        return;
    }

    std::sort(s_route_names.begin(), s_route_names.end());
    (void)repair_degraded_active_route_from_list();
    if (state.mode != TrackerPageState::Mode::Record)
    {
        update_route_page();
    }
}

void sync_active_route_from_config()
{
    auto& state = g_tracker_state;
    app::IAppFacade& app_ctx = app::appFacade();
    const auto& cfg = app_ctx.readConfig();
    if (cfg.route_enabled && cfg.route_path[0] != '\0')
    {
        const char* base = strrchr(cfg.route_path, '/');
        if (base && base[1] != '\0')
        {
            state.active_route = base + 1;
        }
        else
        {
            state.active_route = cfg.route_path;
        }
    }
    else
    {
        state.active_route.clear();
    }
}

void on_start_stop_clicked(lv_event_t*)
{
    if (g_tracker_state.mode == TrackerPageState::Mode::Navigation)
    {
        navigation_dialog::open(g_tracker_state.root, [](const char* path, void*)
                                {
                auto& state = g_tracker_state;
                state.selected_route = path_basename(path);
                refresh_route_list();
                update_route_page();
                open_route_preview_page(); });
        return;
    }
    const bool was_recording = platform::ui::tracker::is_recording();
    if (was_recording)
    {
        platform::ui::tracker::stop_recording();
    }
    else
    {
        if (!platform::ui::tracker::start_recording())
        {
            update_record_status();
            update_start_stop_button();
            if (g_tracker_state.status_label)
            {
                ::ui::i18n::set_label_text(g_tracker_state.status_label, "Start failed");
            }
            return;
        }
    }
    update_record_status();
    update_start_stop_button();
    schedule_deferred_record_list_refresh(120);
}

enum class CategoryAction : uint8_t
{
    View,
    New,
    Current
};

void on_category_action(lv_event_t* event)
{
    const auto value = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    const auto mode = static_cast<TrackerPageState::Mode>(value >> 2);
    const auto action = static_cast<CategoryAction>(value & 3);
    focus_main_panel();
    set_mode(mode);
    if (action == CategoryAction::View) return;
    if (action == CategoryAction::New)
    {
        if (mode == TrackerPageState::Mode::Record && platform::ui::tracker::is_recording()) return;
        on_start_stop_clicked(nullptr);
        if (mode == TrackerPageState::Mode::Navigation || !platform::ui::tracker::is_recording()) return;
    }
    auto& state = g_tracker_state;
    if (mode == TrackerPageState::Mode::Record)
    {
        std::string path;
        if (!platform::ui::tracker::current_path(path)) return;
        refresh_record_list();
        const auto name = path_basename(path);
        const auto found = std::find(s_record_names.begin(), s_record_names.end(), name);
        if (found != s_record_names.end())
        {
            state.selected_record_idx = static_cast<int>(found - s_record_names.begin());
            state.selected_record = name;
            update_record_page();
        }
    }
    else
    {
        state.selected_route = path_basename(app::appFacade().readConfig().route_path);
        update_route_page();
        open_route_preview_page();
    }
}

void show_category_menu(TrackerPageState::Mode mode)
{
    auto* panel = lv_obj_create(g_tracker_state.root);
    const bool recording = platform::ui::tracker::is_recording();
    const auto& cfg = app::appFacade().readConfig();
    const bool current = mode == TrackerPageState::Mode::Record ? recording : cfg.route_enabled && std::string(cfg.route_path).find(std::string(platform::ui::navigation::directory()) + "/") == 0;
    const char* labels[] = {"View", "New", "Current"};
    for (unsigned i = 0; i < 3; ++i)
    {
        auto* item = ::ui::components::bottom_bar::add_button(panel, labels[i], on_category_action,
                                                              reinterpret_cast<void*>((static_cast<uintptr_t>(mode) << 2) | i));
        lv_obj_set_height(item, filter_button_height());
        if ((i == 1 && mode == TrackerPageState::Mode::Record && recording) || (i == 2 && !current)) lv_obj_add_state(item, LV_STATE_DISABLED);
    }
    tracker::ui::input::tracker_show_submenu(panel);
}

void on_mode_record_clicked(lv_event_t*)
{
    show_category_menu(TrackerPageState::Mode::Record);
}

void on_mode_route_clicked(lv_event_t*)
{
    set_mode(TrackerPageState::Mode::Route);
    focus_main_panel();
}

void on_mode_navigation(lv_event_t*)
{
    show_category_menu(TrackerPageState::Mode::Navigation);
}

void on_list_item_clicked(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    auto& state = g_tracker_state;
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    void* ud = lv_obj_get_user_data(target);
    if (!ud)
    {
        return;
    }
    const intptr_t raw = reinterpret_cast<intptr_t>(ud);
    if (raw == kBackListItemUserData)
    {
        if (state.mode == TrackerPageState::Mode::Record)
        {
            state.selected_record_idx = -1;
            state.selected_record.clear();
            update_record_status();
        }
        else
        {
            state.selected_route_idx = -1;
            state.selected_route.clear();
            update_route_status();
        }
        sync_list_item_checked_states();
        focus_mode_panel();
        return;
    }
    if (raw < static_cast<intptr_t>(kListUserDataOffset))
    {
        return;
    }
    const size_t idx = static_cast<size_t>(raw - static_cast<intptr_t>(kListUserDataOffset));

    if (state.mode == TrackerPageState::Mode::Record)
    {
        if (idx >= s_record_names.size())
        {
            return;
        }
        state.selected_record_idx = static_cast<int>(idx);
        state.selected_record = s_record_names[idx].c_str();
        update_record_status();
    }
    else
    {
        if (idx >= s_route_names.size())
        {
            return;
        }
        state.selected_route_idx = static_cast<int>(idx);
        state.selected_route = s_route_names[idx].c_str();
        update_route_status();
    }
    sync_list_item_checked_states();
    open_action_menu_modal();
}

lv_obj_t* create_action_menu_button(lv_obj_t* parent, const char* text)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), action_menu_button_height());

    lv_obj_t* label = lv_label_create(btn);
    ::ui::i18n::set_label_text(label, text);
    lv_obj_center(label);
    ::ui::components::bottom_bar::style_item(btn);
    return btn;
}

void on_action_menu_key(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        modal_close(g_tracker_state.action_menu_modal);
        focus_main_panel();
    }
}

void on_action_menu_item_clicked(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    const auto cmd = static_cast<ActionMenuCommand>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));

    modal_close(g_tracker_state.action_menu_modal);

    switch (cmd)
    {
    case ActionMenuCommand::Load:
        on_route_load_clicked(nullptr);
        update_item_card();
        focus_main_panel();
        break;
    case ActionMenuCommand::Unload:
        on_route_unload_clicked(nullptr);
        update_item_card();
        focus_main_panel();
        break;
    case ActionMenuCommand::Preview:
        open_route_preview_page();
        break;
    case ActionMenuCommand::Delete:
        open_delete_confirm_modal();
        break;
    case ActionMenuCommand::Cancel:
    default:
        focus_main_panel();
        break;
    }
}

void open_action_menu_modal()
{
    auto& state = g_tracker_state;
    if (is_any_modal_open())
    {
        return;
    }

    const bool has_item = state.mode == TrackerPageState::Mode::Record
                              ? state.selected_record_idx >= 0 && state.selected_record_idx < static_cast<int>(s_record_names.size())
                              : state.selected_route_idx >= 0 && state.selected_route_idx < static_cast<int>(s_route_names.size());

    if (!has_item)
    {
        return;
    }

    modal_prepare_group();
    const char* title = (state.mode == TrackerPageState::Mode::Record) ? "Track Actions" : "Route Actions";
    const auto frame = ::ui::components::dialog_frame::create(lv_screen_active(),
                                                              {title, 200, 180, [](lv_event_t*)
                                                               {
                                                                   modal_close(g_tracker_state.action_menu_modal);
                                                                   focus_main_panel();
                                                               },
                                                               on_action_menu_key});
    state.action_menu_modal = frame.overlay;
    lv_group_add_obj(state.modal_group, frame.close);
    lv_obj_t* list = frame.body;
    lv_obj_set_style_pad_row(list, action_menu_row_gap(), LV_PART_MAIN);

    lv_obj_t* first_focus = nullptr;
    auto add_action = [&](ActionMenuCommand cmd, const char* text)
    {
        lv_obj_t* btn = create_action_menu_button(list, text);
        lv_obj_add_event_cb(btn,
                            on_action_menu_item_clicked,
                            LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<uintptr_t>(cmd)));
        lv_obj_add_event_cb(btn, on_action_menu_key, LV_EVENT_KEY, nullptr);
        lv_group_add_obj(state.modal_group, btn);
        if (!first_focus)
        {
            first_focus = btn;
        }
    };

    if (state.mode != TrackerPageState::Mode::Record)
    {
        if (can_load_selected_route())
        {
            add_action(ActionMenuCommand::Load, "Load");
        }
        if (can_unload_active_route())
        {
            add_action(ActionMenuCommand::Unload, "Unload");
        }
        if (can_preview_selected_route())
        {
            add_action(ActionMenuCommand::Preview, "Preview");
        }
    }
    if (can_delete_selected_item())
    {
        add_action(ActionMenuCommand::Delete, "Delete");
    }
    ::ui::components::dialog_frame::fit_content_height(frame);
    ::ui::components::dialog_frame::bind_close_shortcuts(frame, state.modal_group);

    if (first_focus)
    {
        lv_group_focus_obj(first_focus);
    }
}

void on_list_item_focused(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    void* ud = lv_obj_get_user_data(target);
    if (!ud)
    {
        return;
    }
    lv_obj_scroll_to_view(target, LV_ANIM_OFF);
    const intptr_t raw = reinterpret_cast<intptr_t>(ud);
    if (raw == kBackListItemUserData)
    {
        if (lv_obj_t* label = lv_obj_get_child(target, -1))
        {
            ::ui::i18n::set_label_text(label, "Back");
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        }
        return;
    }
    if (raw < static_cast<intptr_t>(kListUserDataOffset))
    {
        return;
    }
    const size_t idx = static_cast<size_t>(raw - static_cast<intptr_t>(kListUserDataOffset));
    const auto& names =
        (g_tracker_state.mode == TrackerPageState::Mode::Record) ? s_record_names : s_route_names;
    if (idx >= names.size())
    {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(target, -1);
    if (!label)
    {
        return;
    }
    ::ui::i18n::set_content_label_text_raw(label, names[idx].c_str());
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
}

void on_list_item_defocused(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    void* ud = lv_obj_get_user_data(target);
    if (!ud)
    {
        return;
    }
    const intptr_t raw = reinterpret_cast<intptr_t>(ud);
    if (raw == kBackListItemUserData)
    {
        if (lv_obj_t* label = lv_obj_get_child(target, -1))
        {
            lv_label_set_text(label, ::ui::i18n::tr("Back"));
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        }
        return;
    }
    if (raw < static_cast<intptr_t>(kListUserDataOffset))
    {
        return;
    }
    const size_t idx = static_cast<size_t>(raw - static_cast<intptr_t>(kListUserDataOffset));
    const auto& names =
        (g_tracker_state.mode == TrackerPageState::Mode::Record) ? s_record_names : s_route_names;
    if (idx >= names.size())
    {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(target, -1);
    if (!label)
    {
        return;
    }
    std::string display_name = format_list_name(names[idx]);
    lv_obj_add_style(label, &s_btn_label, LV_PART_MAIN);
    ::ui::i18n::set_content_label_text_raw(label, display_name.c_str());
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

void on_mode_route_focused(lv_event_t*)
{
    set_mode(TrackerPageState::Mode::Route);
}

void on_route_load_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    if (state.selected_route_idx < 0 || state.selected_route.empty())
    {
        if (state.status_label)
        {
            ::ui::i18n::set_label_text(state.status_label, "Select a route");
        }
        return;
    }
    state.active_route = state.selected_route;
    {
        app::IAppFacade& app_ctx = app::appFacade();
        auto edit = app_ctx.beginConfigEdit();
        if (!edit)
        {
            return;
        }
        edit.config().route_enabled = true;
        char path[96];
        snprintf(path, sizeof(path), "%s", route_path_for_name(state.active_route).c_str());
        strncpy(edit.config().route_path, path, sizeof(edit.config().route_path) - 1);
        edit.config().route_path[sizeof(edit.config().route_path) - 1] = '\0';
        edit.commit(app::AppConfigChangeSet::route());
    }
    update_route_status();
}

void on_route_unload_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    if (state.active_route.empty())
    {
        update_route_status();
        return;
    }
    state.active_route.clear();
    {
        app::IAppFacade& app_ctx = app::appFacade();
        auto edit = app_ctx.beginConfigEdit();
        if (edit)
        {
            edit.config().route_enabled = false;
            edit.config().route_path[0] = '\0';
            edit.commit(app::AppConfigChangeSet::route());
        }
    }
    update_route_status();
}

void on_del_confirm_clicked(lv_event_t*)
{
    auto& state = g_tracker_state;
    bool ok = false;
    if (state.pending_delete_path.empty())
    {
        modal_close(state.del_confirm_modal);
        return;
    }
    std::string path = state.pending_delete_path;

    if (state.pending_delete_mode == TrackerPageState::Mode::Record)
    {
        if (platform::ui::tracker::is_recording())
        {
            std::string current;
            platform::ui::tracker::current_path(current);
            if (!current.empty() && path_basename(current) == state.pending_delete_name)
            {
                if (state.status_label)
                {
                    ::ui::i18n::set_label_text(state.status_label, "Stop recording first");
                }
                modal_close(state.del_confirm_modal);
                return;
            }
        }
        ok = platform::ui::tracker::remove_track(path);
        if (ok)
        {
            state.selected_record_idx = -1;
            state.selected_record.clear();
            refresh_record_list();
            if (state.mode == TrackerPageState::Mode::Record)
            {
                update_record_page();
            }
            update_record_status();
        }
    }
    else
    {
        if (!state.active_route.empty() && state.active_route == state.pending_delete_name)
        {
            app::IAppFacade& app_ctx = app::appFacade();
            auto edit = app_ctx.beginConfigEdit();
            if (edit)
            {
                edit.config().route_enabled = false;
                edit.config().route_path[0] = '\0';
                edit.commit(app::AppConfigChangeSet::route());
            }
            state.active_route.clear();
        }
        ok = platform::ui::route_storage::remove_route(path);
        if (ok)
        {
            state.selected_route_idx = -1;
            state.selected_route.clear();
            refresh_route_list();
            if (state.mode != TrackerPageState::Mode::Record)
            {
                update_route_page();
            }
            update_route_status();
        }
    }

    if (!ok && state.status_label)
    {
        ::ui::i18n::set_label_text(state.status_label, "Delete failed");
    }
    modal_close(state.del_confirm_modal);
}

void on_del_cancel_clicked(lv_event_t*)
{
    modal_close(g_tracker_state.del_confirm_modal);
}

void open_delete_confirm_modal()
{
    auto& state = g_tracker_state;
    if (state.del_confirm_modal)
    {
        return;
    }

    if (state.mode == TrackerPageState::Mode::Record)
    {
        if (state.selected_record_idx < 0 ||
            state.selected_record_idx >= static_cast<int>(s_record_names.size()))
        {
            if (state.status_label)
            {
                ::ui::i18n::set_label_text(state.status_label, "Select a track");
            }
            return;
        }
        state.pending_delete_mode = TrackerPageState::Mode::Record;
        state.pending_delete_name = s_record_names[state.selected_record_idx].c_str();
        std::string path =
            std::string(platform::ui::tracker::track_dir()) + "/" + s_record_names[state.selected_record_idx];
        state.pending_delete_path = path;
    }
    else
    {
        if (state.selected_route_idx < 0 ||
            state.selected_route_idx >= static_cast<int>(s_route_names.size()))
        {
            if (state.status_label)
            {
                ::ui::i18n::set_label_text(state.status_label, "Select a route");
            }
            return;
        }
        state.pending_delete_mode = TrackerPageState::Mode::Route;
        state.pending_delete_name = s_route_names[state.selected_route_idx].c_str();
        std::string path = route_path_for_name(s_route_names[state.selected_route_idx]);
        state.pending_delete_path = path;
    }

    modal_prepare_group();
    const auto frame = ::ui::components::dialog_frame::create(lv_screen_active(),
                                                              {"Delete", 280, 140, on_del_cancel_clicked, [](lv_event_t* event)
                                                               {
                                                                   if (lv_event_get_key(event) == LV_KEY_ESC || lv_event_get_key(event) == LV_KEY_BACKSPACE)
                                                                   {
                                                                       lv_event_stop_bubbling(event);
                                                                       on_del_cancel_clicked(event);
                                                                   }
                                                               }});
    state.del_confirm_modal = frame.overlay;
    lv_obj_t* win = frame.body;
    lv_group_add_obj(state.modal_group, frame.close);

    const std::string msg = ::ui::i18n::format("Delete %s?", state.pending_delete_name.c_str());
    lv_obj_t* label = lv_label_create(win);
    ::ui::i18n::set_label_text_raw(label, msg.c_str());
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);

    lv_obj_t* btn_row = lv_obj_create(win);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* confirm_btn = lv_btn_create(btn_row);
    lv_obj_set_size(confirm_btn, modal_button_width(), action_menu_button_height());
    lv_obj_t* confirm_label = lv_label_create(confirm_btn);
    ::ui::i18n::set_label_text(confirm_label, "Confirm");
    lv_obj_center(confirm_label);
    apply_action_button(confirm_btn, confirm_label);
    lv_obj_add_event_cb(confirm_btn, on_del_confirm_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, modal_button_width(), action_menu_button_height());
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    ::ui::i18n::set_label_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    apply_action_button(cancel_btn, cancel_label);
    lv_obj_add_event_cb(cancel_btn, on_del_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    lv_group_add_obj(state.modal_group, confirm_btn);
    lv_group_add_obj(state.modal_group, cancel_btn);
    ::ui::components::dialog_frame::bind_close_shortcuts(frame, state.modal_group);
    lv_group_focus_obj(cancel_btn);
}

} // namespace

void refresh_page()
{
    if (g_tracker_state.card_location_model) g_tracker_state.card_location_model->file.clear();
    sync_active_route_from_config();
    update_mode_buttons();
    update_record_status();
    update_start_stop_button();
    refresh_record_list();
    refresh_route_list();
    update_route_status();
    tracker::ui::input::tracker_input_on_ui_refreshed();
    ui_update_top_bar_battery(g_tracker_state.top_bar);
}

void init_page(lv_obj_t* parent)
{
    auto& state = g_tracker_state;
    if (!parent)
    {
        return;
    }

    if (state.root)
    {
        cleanup_page();
    }

    state.root = layout::create_root(parent);
    state.header = layout::create_header(state.root);
    state.content = layout::create_content(state.root);
    state.filter_panel = layout::create_filter_panel(state.content, filter_panel_width());
    state.list_panel = layout::create_list_panel(state.content);

    state.mode_record_btn = lv_btn_create(state.filter_panel);
    lv_obj_set_width(state.mode_record_btn, LV_PCT(100));
    lv_obj_set_height(state.mode_record_btn, filter_button_height());
    state.mode_record_label = lv_label_create(state.mode_record_btn);
    lv_obj_add_style(state.mode_record_label, &s_btn_label, LV_PART_MAIN);
    ::ui::i18n::set_label_text(state.mode_record_label, "Tracks");
    lv_obj_center(state.mode_record_label);

    state.mode_route_btn = lv_btn_create(state.filter_panel);
    lv_obj_set_width(state.mode_route_btn, LV_PCT(100));
    lv_obj_set_height(state.mode_route_btn, filter_button_height());
    state.mode_route_label = lv_label_create(state.mode_route_btn);
    lv_obj_add_style(state.mode_route_label, &s_btn_label, LV_PART_MAIN);
    ::ui::i18n::set_label_text(state.mode_route_label, "Routes");
    lv_obj_center(state.mode_route_label);

    if (platform::ui::navigation::supported())
    {
        state.mode_navigation_btn = lv_btn_create(state.filter_panel);
        lv_obj_set_size(state.mode_navigation_btn, LV_PCT(100), filter_button_height());
        state.mode_navigation_label = lv_label_create(state.mode_navigation_btn);
        lv_obj_add_style(state.mode_navigation_label, &s_btn_label, LV_PART_MAIN);
        ::ui::i18n::set_label_text(state.mode_navigation_label, "Nav");
        lv_obj_center(state.mode_navigation_label);
        lv_obj_add_event_cb(state.mode_navigation_btn, on_mode_navigation, LV_EVENT_CLICKED, nullptr);
    }

    state.status_label = lv_label_create(state.list_panel);
    ::ui::i18n::set_label_text(state.status_label, "Not recording");
    lv_obj_set_width(state.status_label, LV_PCT(100));
    lv_obj_set_style_text_font(
        state.status_label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    lv_obj_set_style_text_color(state.status_label, lv_color_hex(kPanelTextMuted), 0);

    state.list_container = layout::create_list_container(state.list_panel);
    lv_obj_add_flag(state.list_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(state.list_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(state.list_container, LV_SCROLLBAR_MODE_AUTO);

    state.bottom_bar = ::ui::components::bottom_bar::create(state.root);
    state.start_stop_btn = ::ui::components::bottom_bar::add_button(state.bottom_bar, "Stop recording", on_start_stop_clicked);
    state.start_stop_label = lv_obj_get_child(state.start_stop_btn, 0);

    ::ui::widgets::TopBarConfig cfg;
    cfg.height = page_profile().top_bar_height;
    ::ui::widgets::top_bar_init(state.top_bar, state.header, cfg);
    ::ui::widgets::top_bar_set_title(state.top_bar, ::ui::i18n::tr("Tracker"));
    ::ui::widgets::top_bar_set_back_callback(state.top_bar, on_back, nullptr);

    lv_obj_add_event_cb(state.mode_record_btn, on_mode_record_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.mode_route_btn, on_mode_route_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(state.mode_route_btn, on_mode_route_focused, LV_EVENT_FOCUSED, nullptr);

    set_mode(TrackerPageState::Mode::Record);
    refresh_page();
}

void cleanup_page()
{
    navigation_dialog::close();
    auto& state = g_tracker_state;
    if (state.card_timer)
    {
        lv_timer_del(state.card_timer);
        state.card_timer = nullptr;
    }
    ::platform::memory::destroyPsram(state.card_location_model);
    state.card_location_model = nullptr;
    cancel_deferred_record_list_refresh();
    if (state.action_menu_modal)
    {
        lv_obj_del(state.action_menu_modal);
        state.action_menu_modal = nullptr;
    }
    if (state.del_confirm_modal)
    {
        lv_obj_del(state.del_confirm_modal);
        state.del_confirm_modal = nullptr;
    }
    if (state.route_preview_help_modal)
    {
        lv_obj_del(state.route_preview_help_modal);
        state.route_preview_help_modal = nullptr;
    }
    if (s_preview_download_poll_timer)
    {
        lv_timer_del(s_preview_download_poll_timer);
        s_preview_download_poll_timer = nullptr;
    }
    destroy_route_preview_image_strip(true);
    ::ui::widgets::map::destroy(s_preview_map_runtime);
    s_preview_map_loader_paused = false;
    if (state.route_preview_page)
    {
        lv_obj_del(state.route_preview_page);
        state.route_preview_page = nullptr;
    }
    state.route_preview_map_host = nullptr;
    state.route_preview_elevation_panel = nullptr;
    state.route_preview_status_label = nullptr;
    state.route_preview_progress = nullptr;
    state.route_preview_download_btn = nullptr;
    state.route_preview_load_btn = nullptr;
    state.route_preview_load_label = nullptr;
    if (state.modal_group)
    {
        lv_group_del(state.modal_group);
        state.modal_group = nullptr;
    }
    clear_preview_model();
    state.prev_group = nullptr;
    if (state.root)
    {
        lv_obj_del(state.root);
    }
    state = TrackerPageState{};
}

} // namespace components
} // namespace ui
} // namespace tracker
