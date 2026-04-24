/**
 * @file node_info_page_components.cpp
 * @brief Node info page UI components
 */

#include "ui/screens/node_info/node_info_page_components.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/usecase/contact_service.h"
#include "platform/ui/gps_runtime.h"
#include "sys/clock.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/menu/dashboard/dashboard_style.h"
#include "ui/page/page_profile.h"
#include "ui/screens/node_info/node_info_page_layout.h"
#include "ui/ui_common.h"
#include "ui/ui_theme.h"
#include "ui/widgets/map/map_tiles.h"
#include "ui/widgets/map/map_viewport.h"
#include "ui/widgets/top_bar.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern void show_toast(const char* message, uint32_t duration_ms);

namespace node_info
{
namespace ui
{

namespace
{

namespace dashboard = ::ui::menu::dashboard;
namespace map_viewport = ::ui::widgets::map;

#define NODE_INFO_LOG(...) std::printf("[NodeInfo] " __VA_ARGS__)

NodeInfoWidgets s_widgets;
::ui::widgets::TopBar s_top_bar;

struct NodeInfoRuntimeState
{
    bool has_node = false;
    bool map_ready = false;
    chat::contacts::NodeInfo node{};
    map_viewport::GeoPoint self_point{};
    int zoom = map_viewport::kDefaultZoom;
    int pan_x = 0;
    int pan_y = 0;
    int drag_start_pan_x = 0;
    int drag_start_pan_y = 0;
    lv_point_precise_t link_points[2]{};
    map_viewport::Runtime viewport{};
};

NodeInfoRuntimeState s_state;

struct LayerPopupState
{
    lv_obj_t* bg = nullptr;
    lv_obj_t* win = nullptr;
    lv_obj_t* summary_row = nullptr;
    lv_obj_t* source_summary = nullptr;
    lv_obj_t* contour_summary = nullptr;
    lv_obj_t* source_btns[3] = {nullptr, nullptr, nullptr};
    lv_obj_t* contour_btn = nullptr;
    lv_obj_t* close_btn = nullptr;
    lv_group_t* group = nullptr;
    lv_group_t* prev_group = nullptr;
    uint32_t close_ms = 0;
    bool open = false;
};

LayerPopupState s_layer_popup;

struct ViewMetrics
{
    lv_coord_t width = 0;
    lv_coord_t height = 0;
    lv_coord_t pad = 12;
    lv_coord_t right_col_w = 120;
    lv_coord_t right_x = 0;
    lv_coord_t left_w = 0;
    lv_coord_t focus_x = 0;
    lv_coord_t focus_y = 0;
    lv_coord_t info_top = 0;
    lv_coord_t info_line_h = 16;
    lv_coord_t info_gap = 5;
    lv_coord_t zoom_size = 30;
    lv_coord_t zoom_gap = 8;
    lv_coord_t layer_w = 72;
    lv_coord_t layer_h = 26;
    bool compact = true;
};

constexpr uint32_t kLayerPopupDebounceMs = 300;

constexpr uint32_t kHexAmber = 0xEBA341;
constexpr uint32_t kHexAmberDark = 0xC98118;
constexpr uint32_t kHexWarmBg = 0xF6E6C6;
constexpr uint32_t kHexPanelBg = 0xFAF0D8;
constexpr uint32_t kHexLine = 0xE7C98F;
constexpr uint32_t kHexText = 0x6B4A1E;
constexpr uint32_t kHexTextDim = 0x8A6A3A;
constexpr uint32_t kHexWarn = 0xB94A2C;
constexpr uint32_t kHexOk = 0x3E7D3E;
constexpr uint32_t kHexInfo = 0x2D6FB6;
constexpr uint32_t kHexReadoutAmber = 0xFFC75A;
constexpr uint32_t kHexReadoutBlue = 0x68D5FF;
constexpr uint32_t kHexReadoutGreen = 0x8BEA7B;
constexpr uint32_t kHexReadoutLight = 0xFFF1D2;
constexpr uint32_t kHexReadoutLightStrong = 0xFFE6A9;
constexpr lv_coord_t kInfoPanelPadX = 8;
constexpr lv_coord_t kInfoPanelPadY = 6;
constexpr lv_coord_t kInfoPanelRadius = 8;

static const lv_color_t kColorBackdrop = lv_color_hex(kHexWarmBg);
static const lv_color_t kColorBackdropAlt = lv_color_hex(kHexPanelBg);
static const lv_color_t kColorTextPrimary = lv_color_hex(kHexText);
static const lv_color_t kColorId = lv_color_hex(kHexAmberDark);
static const lv_color_t kColorLon = lv_color_hex(kHexInfo);
static const lv_color_t kColorLat = lv_color_hex(kHexOk);
static const lv_color_t kColorDistance = lv_color_hex(kHexAmberDark);
static const lv_color_t kColorNodeMarker = lv_color_hex(kHexWarn);
static const lv_color_t kColorSelfMarker = lv_color_hex(kHexInfo);
static const lv_color_t kColorLink = lv_color_hex(kHexAmberDark);
static const lv_color_t kColorMuted = lv_color_hex(kHexTextDim);
static const lv_color_t kColorButtonBg = lv_color_hex(kHexPanelBg);
static const lv_color_t kColorButtonDisabled = lv_color_hex(kHexLine);
static const lv_color_t kColorButtonFocus = lv_color_hex(kHexAmber);
static const lv_color_t kColorReadoutProtocol = lv_color_hex(kHexReadoutAmber);
static const lv_color_t kColorReadoutRssi = lv_color_hex(kHexReadoutBlue);
static const lv_color_t kColorReadoutSnr = lv_color_hex(kHexReadoutGreen);
static const lv_color_t kColorReadoutSeen = lv_color_hex(kHexReadoutLight);
static const lv_color_t kColorReadoutZoom = lv_color_hex(kHexReadoutLightStrong);

bool is_hidden(lv_obj_t* obj)
{
    return obj ? lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) : true;
}

void log_widget_box(const char* stage, const char* name, lv_obj_t* obj)
{
    if (!obj)
    {
        NODE_INFO_LOG("%s %s=<null>\n", stage, name);
        return;
    }

    NODE_INFO_LOG("%s %s=%p parent=%p hidden=%d pos=(%d,%d) size=%dx%d\n",
                  stage,
                  name,
                  obj,
                  lv_obj_get_parent(obj),
                  is_hidden(obj) ? 1 : 0,
                  static_cast<int>(lv_obj_get_x(obj)),
                  static_cast<int>(lv_obj_get_y(obj)),
                  static_cast<int>(lv_obj_get_width(obj)),
                  static_cast<int>(lv_obj_get_height(obj)));
}

void log_geo_point(const char* stage, const char* name, const map_viewport::GeoPoint& point)
{
    if (!point.valid)
    {
        NODE_INFO_LOG("%s %s=invalid\n", stage, name);
        return;
    }

    NODE_INFO_LOG("%s %s lat=%.7f lon=%.7f\n", stage, name, point.lat, point.lon);
}

void log_view_metrics(const char* stage, const ViewMetrics& metrics)
{
    NODE_INFO_LOG(
        "%s metrics width=%d height=%d pad=%d right_col_w=%d right_x=%d left_w=%d focus=(%d,%d) info_top=%d line_h=%d gap=%d zoom_size=%d layer=%dx%d compact=%d\n",
        stage,
        static_cast<int>(metrics.width),
        static_cast<int>(metrics.height),
        static_cast<int>(metrics.pad),
        static_cast<int>(metrics.right_col_w),
        static_cast<int>(metrics.right_x),
        static_cast<int>(metrics.left_w),
        static_cast<int>(metrics.focus_x),
        static_cast<int>(metrics.focus_y),
        static_cast<int>(metrics.info_top),
        static_cast<int>(metrics.info_line_h),
        static_cast<int>(metrics.info_gap),
        static_cast<int>(metrics.zoom_size),
        static_cast<int>(metrics.layer_w),
        static_cast<int>(metrics.layer_h),
        metrics.compact ? 1 : 0);
}

void log_scene_widgets(const char* stage)
{
    log_widget_box(stage, "root", s_widgets.root);
    log_widget_box(stage, "header", s_widgets.header);
    log_widget_box(stage, "content", s_widgets.content);
    log_widget_box(stage, "map_stage", s_widgets.map_stage);
    log_widget_box(stage, "tile_layer", s_widgets.tile_layer);
    log_widget_box(stage, "map_overlay_layer", s_widgets.map_overlay_layer);
    log_widget_box(stage, "map_gesture_surface", s_widgets.map_gesture_surface);
    log_widget_box(stage, "back_btn", s_widgets.back_btn);
    log_widget_box(stage, "title_label", s_widgets.title_label);
    log_widget_box(stage, "id_label", s_widgets.id_label);
    log_widget_box(stage, "lon_label", s_widgets.lon_label);
    log_widget_box(stage, "lat_label", s_widgets.lat_label);
    log_widget_box(stage, "no_position_label", s_widgets.no_position_label);
    log_widget_box(stage, "info_panel", s_widgets.info_panel);
    log_widget_box(stage, "zoom_in_btn", s_widgets.zoom_in_btn);
    log_widget_box(stage, "zoom_out_btn", s_widgets.zoom_out_btn);
    log_widget_box(stage, "zoom_status_label", s_widgets.zoom_status_label);
    log_widget_box(stage, "layer_btn", s_widgets.layer_btn);
    const auto viewport_status = map_viewport::status(s_state.viewport);
    NODE_INFO_LOG("%s viewport alive=%d focus=%d anchor=%d zoom=%d pan=%d,%d map_data=%d visible_map=%d\n",
                  stage,
                  viewport_status.alive ? 1 : 0,
                  viewport_status.has_focus ? 1 : 0,
                  viewport_status.anchor_valid ? 1 : 0,
                  viewport_status.zoom,
                  viewport_status.pan_x,
                  viewport_status.pan_y,
                  viewport_status.has_map_data ? 1 : 0,
                  viewport_status.has_visible_map_data ? 1 : 0);
}

void log_node_summary(const char* stage, const chat::contacts::NodeInfo& node)
{
    NODE_INFO_LOG(
        "%s node id=%08" PRIX32 " protocol=%d channel=%u last_seen=%" PRIu32 " rssi=%.1f snr=%.1f hops=%u next_hop=%u via_mqtt=%d ignored=%d display='%s' short='%s' long='%s'\n",
        stage,
        node.node_id,
        static_cast<int>(node.protocol),
        static_cast<unsigned>(node.channel),
        node.last_seen,
        static_cast<double>(node.rssi),
        static_cast<double>(node.snr),
        static_cast<unsigned>(node.hops_away),
        static_cast<unsigned>(node.next_hop),
        node.via_mqtt ? 1 : 0,
        node.is_ignored ? 1 : 0,
        node.display_name.c_str(),
        node.short_name,
        node.long_name);
    NODE_INFO_LOG(
        "%s node position valid=%d lat_i=%" PRId32 " lon_i=%" PRId32 " alt=%ld has_alt=%d gps_acc_mm=%" PRIu32 " pdop=%" PRIu32 " hdop=%" PRIu32 " vdop=%" PRIu32 "\n",
        stage,
        node.position.valid ? 1 : 0,
        node.position.latitude_i,
        node.position.longitude_i,
        static_cast<long>(node.position.altitude),
        node.position.has_altitude ? 1 : 0,
        node.position.gps_accuracy_mm,
        node.position.pdop,
        node.position.hdop,
        node.position.vdop);
}

const lv_font_t* font_montserrat_12_safe()
{
#if defined(LV_FONT_MONTSERRAT_12) && LV_FONT_MONTSERRAT_12
    return &lv_font_montserrat_12;
#else
    return LV_FONT_DEFAULT;
#endif
}

const lv_font_t* font_montserrat_14_safe()
{
#if defined(LV_FONT_MONTSERRAT_14) && LV_FONT_MONTSERRAT_14
    return &lv_font_montserrat_14;
#else
    return font_montserrat_12_safe();
#endif
}

const lv_font_t* font_montserrat_16_safe()
{
#if defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
    return &lv_font_montserrat_16;
#else
    return font_montserrat_14_safe();
#endif
}

const lv_font_t* font_montserrat_18_safe()
{
#if defined(LV_FONT_MONTSERRAT_18) && LV_FONT_MONTSERRAT_18
    return &lv_font_montserrat_18;
#else
    return font_montserrat_16_safe();
#endif
}

const lv_font_t* font_montserrat_22_safe()
{
#if defined(LV_FONT_MONTSERRAT_22) && LV_FONT_MONTSERRAT_22
    return &lv_font_montserrat_22;
#else
    return font_montserrat_18_safe();
#endif
}

lv_coord_t clamp_coord(lv_coord_t value, lv_coord_t min_value, lv_coord_t max_value)
{
    if (max_value < min_value)
    {
        max_value = min_value;
    }
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

double clamp_double(double value, double min_value, double max_value)
{
    if (max_value < min_value)
    {
        max_value = min_value;
    }
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

void make_plain(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
}

void set_label_text(lv_obj_t* label, const char* text)
{
    if (!label || !text)
    {
        return;
    }
    lv_label_set_text(label, text);
    ::ui::fonts::apply_localized_font(label, text, lv_obj_get_style_text_font(label, LV_PART_MAIN));
}

lv_obj_t* create_label(lv_obj_t* parent,
                       const char* text,
                       const lv_font_t* font,
                       lv_color_t color)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    ::ui::fonts::apply_localized_font(label, text, font);
    return label;
}

void apply_info_panel_style(lv_obj_t* panel)
{
    if (!panel)
    {
        return;
    }

    make_plain(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(panel, kInfoPanelRadius, 0);
    lv_obj_set_style_bg_color(panel, kColorBackdropAlt, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_60, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_set_style_outline_width(panel, 0, 0);
}

void apply_zoom_button_style(lv_obj_t* button, lv_obj_t* label)
{
    if (!button)
    {
        return;
    }

    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, kColorButtonBg, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(kHexAmberDark), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, kColorButtonDisabled, LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_DISABLED);
    lv_obj_set_style_border_color(button, lv_color_hex(kHexLine), LV_STATE_DISABLED);
    lv_obj_set_style_outline_width(button, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(button, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(button, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(button, kColorButtonFocus, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(button, kColorButtonFocus, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(button, 1, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(button, 1, LV_STATE_FOCUS_KEY);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(button, LV_SCROLLBAR_MODE_OFF);

    if (label)
    {
        lv_obj_set_style_text_font(label, font_montserrat_22_safe(), 0);
        lv_obj_set_style_text_color(label, kColorTextPrimary, 0);
        ::ui::fonts::apply_localized_font(label, lv_label_get_text(label), font_montserrat_22_safe());
    }
}

void apply_layer_button_style(lv_obj_t* button, lv_obj_t* label, bool compact)
{
    if (!button)
    {
        return;
    }

    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_bg_color(button, kColorButtonBg, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(kHexAmberDark), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_outline_width(button, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(button, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(button, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(button, kColorButtonFocus, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(button, kColorButtonFocus, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(button, 1, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(button, 1, LV_STATE_FOCUS_KEY);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(button, LV_SCROLLBAR_MODE_OFF);

    if (label)
    {
        const lv_font_t* font = compact ? font_montserrat_12_safe() : font_montserrat_14_safe();
        lv_obj_set_style_text_font(label, font, 0);
        lv_obj_set_style_text_color(label, kColorTextPrimary, 0);
        ::ui::fonts::apply_localized_font(label, lv_label_get_text(label), font);
    }
}

void update_layer_popup_button_selected(lv_obj_t* btn, bool selected)
{
    if (!btn)
    {
        return;
    }

    lv_obj_set_style_bg_color(btn, selected ? lv_color_hex(kHexAmber) : kColorButtonBg, 0);
    lv_obj_set_style_border_color(btn,
                                  selected ? lv_color_hex(kHexAmberDark) : lv_color_hex(kHexLine),
                                  0);
    lv_obj_set_style_outline_width(btn, selected ? 2 : 0, 0);
    lv_obj_set_style_outline_color(btn, lv_color_hex(kHexAmberDark), 0);
    lv_obj_set_style_outline_pad(btn, 0, 0);

    if (lv_obj_t* label = lv_obj_get_child(btn, 0))
    {
        lv_obj_set_style_text_color(label, kColorTextPrimary, 0);
    }
}

bool is_layer_popup_open()
{
    return s_layer_popup.open &&
           s_layer_popup.bg != nullptr &&
           lv_obj_is_valid(s_layer_popup.bg);
}

void render_scene();
void render_connection_and_markers(const map_viewport::GeoPoint& node_point, const ViewMetrics& metrics);
void update_gesture_availability(bool enabled);

void apply_layer_notice(const map_viewport::LayerNotice& notice)
{
    if (notice.has_message)
    {
        show_toast(notice.message, notice.duration_ms > 0 ? notice.duration_ms : 1500);
    }
}

lv_coord_t layer_popup_button_height(bool touch_layout)
{
    return touch_layout ? 38 : 20;
}

void refresh_layer_popup_labels()
{
    if (!is_layer_popup_open())
    {
        return;
    }

    const auto layer_state = map_viewport::current_layer_state();

    if (s_layer_popup.source_summary)
    {
        const std::string text = map_viewport::layer_base_summary_text(layer_state.map_source);
        set_label_text(s_layer_popup.source_summary, text.c_str());
    }
    if (s_layer_popup.contour_summary)
    {
        ::ui::i18n::set_label_text(s_layer_popup.contour_summary,
                                   map_viewport::layer_contour_status_key(layer_state.contour_enabled));
    }

    for (std::size_t index = 0; index < 3; ++index)
    {
        update_layer_popup_button_selected(s_layer_popup.source_btns[index],
                                           index == layer_state.map_source);
    }
    update_layer_popup_button_selected(s_layer_popup.contour_btn, layer_state.contour_enabled);
    if (s_layer_popup.contour_btn)
    {
        lv_obj_t* label = lv_obj_get_child(s_layer_popup.contour_btn, 0);
        if (label)
        {
            ::ui::i18n::set_label_text(label,
                                       map_viewport::layer_contour_status_key(layer_state.contour_enabled));
        }
    }
}

void close_layer_popup();

void on_layer_popup_source_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    const uint8_t map_source =
        static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    NODE_INFO_LOG("layer_popup source_click source=%u\n", static_cast<unsigned>(map_source));
    map_viewport::LayerNotice notice{};
    const bool changed = map_viewport::set_layer_map_source(map_source, &notice);
    apply_layer_notice(notice);
    if (changed)
    {
        render_scene();
    }
    refresh_layer_popup_labels();
}

void on_layer_popup_contour_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    NODE_INFO_LOG("layer_popup contour_click\n");
    map_viewport::LayerNotice notice{};
    map_viewport::toggle_layer_contour(&notice);
    apply_layer_notice(notice);
    render_scene();
    refresh_layer_popup_labels();
}

void on_layer_popup_close_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        close_layer_popup();
    }
}

void on_layer_popup_key(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_KEY)
    {
        return;
    }

    const lv_key_t key = static_cast<lv_key_t>(lv_event_get_key(e));
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        close_layer_popup();
    }
}

void on_layer_popup_bg_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (lv_event_get_target_obj(e) == s_layer_popup.bg)
    {
        close_layer_popup();
    }
}

void position_layer_popup_window(lv_obj_t* screen, lv_obj_t* win, lv_coord_t width, lv_coord_t height)
{
    if (!screen || !win)
    {
        return;
    }

    const lv_coord_t screen_w = lv_obj_get_width(screen);
    const lv_coord_t screen_h = lv_obj_get_height(screen);
    const lv_coord_t gap = ::ui::page_profile::current().large_touch_hitbox ? 12 : 8;

    lv_coord_t desired_x = (screen_w - width) / 2;
    lv_coord_t desired_y = screen_h - height - gap;

    if (s_widgets.layer_btn && lv_obj_is_valid(s_widgets.layer_btn))
    {
        lv_area_t layer_area{};
        lv_obj_get_coords(s_widgets.layer_btn, &layer_area);
        desired_x = ((layer_area.x1 + layer_area.x2 + 1) - width) / 2;
        desired_y = layer_area.y1 - height - gap;
    }

    const lv_coord_t min_x = gap;
    const lv_coord_t max_x = screen_w - width - gap;
    const lv_coord_t top_safe = ::ui::page_profile::current().top_bar_height + gap;
    const lv_coord_t max_y = screen_h - height - gap;
    if (desired_y < top_safe)
    {
        desired_y = top_safe;
    }

    lv_obj_set_pos(win,
                   clamp_coord(desired_x, min_x, max_x),
                   clamp_coord(desired_y, top_safe, max_y));
}

lv_obj_t* create_layer_popup_button(lv_obj_t* parent,
                                    const char* text,
                                    lv_event_cb_t cb,
                                    uintptr_t user_data)
{
    if (!parent)
    {
        return nullptr;
    }

    const bool compact = !::ui::page_profile::current().large_touch_hitbox;
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_height(btn, layer_popup_button_height(!compact));
    apply_layer_button_style(btn, nullptr, compact);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(user_data));
    lv_obj_add_event_cb(btn, on_layer_popup_key, LV_EVENT_KEY, nullptr);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, ::ui::i18n::tr(text));
    apply_layer_button_style(btn, label, compact);
    lv_obj_center(label);
    return btn;
}

void open_layer_popup()
{
    if (!s_widgets.root || !lv_obj_is_valid(s_widgets.root))
    {
        NODE_INFO_LOG("layer_popup open skipped: root invalid\n");
        return;
    }
    if (is_layer_popup_open())
    {
        NODE_INFO_LOG("layer_popup open skipped: already open\n");
        return;
    }

    const uint32_t now = sys::millis_now();
    if (s_layer_popup.close_ms > 0 && (now - s_layer_popup.close_ms) < kLayerPopupDebounceMs)
    {
        NODE_INFO_LOG("layer_popup open skipped: debounce=%lu\n",
                      static_cast<unsigned long>(now - s_layer_popup.close_ms));
        return;
    }

    lv_obj_t* screen = lv_screen_active();
    if (!screen)
    {
        NODE_INFO_LOG("layer_popup open skipped: screen missing\n");
        return;
    }

    const bool touch_layout = ::ui::page_profile::current().large_touch_hitbox;
    const auto modal_size =
        ::ui::page_profile::resolve_modal_size(touch_layout ? 260 : 212,
                                               touch_layout ? 224 : 178,
                                               screen);

    s_layer_popup.bg = lv_obj_create(screen);
    lv_obj_set_size(s_layer_popup.bg, lv_obj_get_width(screen), lv_obj_get_height(screen));
    lv_obj_set_pos(s_layer_popup.bg, 0, 0);
    make_plain(s_layer_popup.bg);
    lv_obj_set_style_bg_color(s_layer_popup.bg, lv_color_hex(kHexWarmBg), 0);
    lv_obj_set_style_bg_opa(s_layer_popup.bg, LV_OPA_20, 0);
    lv_obj_add_event_cb(s_layer_popup.bg, on_layer_popup_bg_clicked, LV_EVENT_CLICKED, nullptr);

    s_layer_popup.win = lv_obj_create(s_layer_popup.bg);
    lv_obj_set_size(s_layer_popup.win, modal_size.width, modal_size.height);
    make_plain(s_layer_popup.win);
    lv_obj_set_style_bg_color(s_layer_popup.win, lv_color_hex(kHexPanelBg), 0);
    lv_obj_set_style_bg_opa(s_layer_popup.win, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_layer_popup.win, 2, 0);
    lv_obj_set_style_border_color(s_layer_popup.win, lv_color_hex(kHexAmberDark), 0);
    lv_obj_set_style_radius(s_layer_popup.win, 10, 0);
    lv_obj_set_style_pad_all(s_layer_popup.win, touch_layout ? 12 : 6, 0);
    lv_obj_set_style_pad_row(s_layer_popup.win, touch_layout ? 8 : 3, 0);
    lv_obj_set_flex_flow(s_layer_popup.win, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_layer_popup.win,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    position_layer_popup_window(screen, s_layer_popup.win, modal_size.width, modal_size.height);

    lv_obj_t* title = lv_label_create(s_layer_popup.win);
    ::ui::i18n::set_label_text(title, "Map Layer");
    lv_obj_set_style_text_font(title, touch_layout ? font_montserrat_16_safe() : font_montserrat_14_safe(),
                               0);
    lv_obj_set_style_text_color(title, kColorTextPrimary, 0);

    s_layer_popup.summary_row = lv_obj_create(s_layer_popup.win);
    make_plain(s_layer_popup.summary_row);
    lv_obj_set_width(s_layer_popup.summary_row, LV_PCT(100));
    lv_obj_set_height(s_layer_popup.summary_row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(s_layer_popup.summary_row, touch_layout ? 10 : 8, 0);
    lv_obj_set_flex_flow(s_layer_popup.summary_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_layer_popup.summary_row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    s_layer_popup.source_summary = lv_label_create(s_layer_popup.summary_row);
    lv_obj_set_width(s_layer_popup.source_summary, LV_PCT(56));
    lv_label_set_long_mode(s_layer_popup.source_summary, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(s_layer_popup.source_summary, font_montserrat_12_safe(), 0);
    lv_obj_set_style_text_color(s_layer_popup.source_summary, kColorMuted, 0);
    lv_obj_set_style_text_align(s_layer_popup.source_summary, LV_TEXT_ALIGN_LEFT, 0);

    s_layer_popup.contour_summary = lv_label_create(s_layer_popup.summary_row);
    lv_obj_set_width(s_layer_popup.contour_summary, LV_PCT(44));
    lv_label_set_long_mode(s_layer_popup.contour_summary, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(s_layer_popup.contour_summary, font_montserrat_12_safe(), 0);
    lv_obj_set_style_text_color(s_layer_popup.contour_summary, kColorMuted, 0);
    lv_obj_set_style_text_align(s_layer_popup.contour_summary, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* list = lv_obj_create(s_layer_popup.win);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_height(list, 0);
    lv_obj_set_flex_grow(list, 1);
    make_plain(list);
    lv_obj_set_style_pad_row(list, touch_layout ? 6 : 2, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_layer_popup.source_btns[0] =
        create_layer_popup_button(list,
                                  map_viewport::layer_map_source_label_key(0),
                                  on_layer_popup_source_clicked,
                                  static_cast<uintptr_t>(0));
    s_layer_popup.source_btns[1] = create_layer_popup_button(
        list,
        map_viewport::layer_map_source_label_key(1),
        on_layer_popup_source_clicked,
        static_cast<uintptr_t>(1));
    s_layer_popup.source_btns[2] = create_layer_popup_button(
        list,
        map_viewport::layer_map_source_label_key(2),
        on_layer_popup_source_clicked,
        static_cast<uintptr_t>(2));
    s_layer_popup.contour_btn =
        create_layer_popup_button(
            list, map_viewport::layer_contour_status_key(false), on_layer_popup_contour_clicked, 0);
    s_layer_popup.close_btn = create_layer_popup_button(list, "Close", on_layer_popup_close_clicked, 0);

    if (!s_layer_popup.group)
    {
        s_layer_popup.group = lv_group_create();
    }
    lv_group_remove_all_objs(s_layer_popup.group);
    s_layer_popup.prev_group = lv_group_get_default();
    set_default_group(s_layer_popup.group);
    for (lv_obj_t* btn : s_layer_popup.source_btns)
    {
        if (btn)
        {
            lv_group_add_obj(s_layer_popup.group, btn);
        }
    }
    if (s_layer_popup.contour_btn)
    {
        lv_group_add_obj(s_layer_popup.group, s_layer_popup.contour_btn);
    }
    if (s_layer_popup.close_btn)
    {
        lv_group_add_obj(s_layer_popup.group, s_layer_popup.close_btn);
    }

    s_layer_popup.open = true;
    s_layer_popup.close_ms = 0;
    refresh_layer_popup_labels();
    update_gesture_availability(false);

    const auto layer_state = map_viewport::current_layer_state();
    lv_obj_t* focus_btn = s_layer_popup.source_btns[layer_state.map_source < 3 ? layer_state.map_source : 0];
    if (!focus_btn)
    {
        focus_btn = s_layer_popup.close_btn;
    }
    if (focus_btn)
    {
        lv_group_focus_obj(focus_btn);
    }
    NODE_INFO_LOG("layer_popup open map_source=%u contour=%d pos=(%d,%d) size=%dx%d\n",
                  static_cast<unsigned>(layer_state.map_source),
                  layer_state.contour_enabled ? 1 : 0,
                  static_cast<int>(lv_obj_get_x(s_layer_popup.win)),
                  static_cast<int>(lv_obj_get_y(s_layer_popup.win)),
                  static_cast<int>(lv_obj_get_width(s_layer_popup.win)),
                  static_cast<int>(lv_obj_get_height(s_layer_popup.win)));
}

void close_layer_popup()
{
    if (!is_layer_popup_open())
    {
        return;
    }

    if (s_layer_popup.prev_group)
    {
        set_default_group(s_layer_popup.prev_group);
        if (s_widgets.layer_btn && lv_obj_is_valid(s_widgets.layer_btn))
        {
            lv_group_focus_obj(s_widgets.layer_btn);
        }
    }

    if (s_layer_popup.bg && lv_obj_is_valid(s_layer_popup.bg))
    {
        lv_obj_del(s_layer_popup.bg);
    }

    s_layer_popup.bg = nullptr;
    s_layer_popup.win = nullptr;
    s_layer_popup.summary_row = nullptr;
    s_layer_popup.source_summary = nullptr;
    s_layer_popup.contour_summary = nullptr;
    s_layer_popup.source_btns[0] = nullptr;
    s_layer_popup.source_btns[1] = nullptr;
    s_layer_popup.source_btns[2] = nullptr;
    s_layer_popup.contour_btn = nullptr;
    s_layer_popup.close_btn = nullptr;
    s_layer_popup.prev_group = nullptr;
    s_layer_popup.close_ms = sys::millis_now();
    s_layer_popup.open = false;
    update_gesture_availability(s_state.has_node && s_state.node.position.valid);
    NODE_INFO_LOG("layer_popup close\n");
}

void on_layer_button_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        NODE_INFO_LOG("layer_button clicked\n");
        open_layer_popup();
    }
}

void apply_marker_style(lv_obj_t* obj, lv_coord_t size, lv_color_t color, bool filled)
{
    if (!obj)
    {
        return;
    }

    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(obj, filled ? 0 : 2, 0);
    lv_obj_set_style_border_color(obj, color, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

ViewMetrics view_metrics()
{
    ViewMetrics metrics{};
    const auto& profile = ::ui::page_profile::current();

    metrics.width = s_widgets.content ? lv_obj_get_width(s_widgets.content) : 0;
    metrics.height = s_widgets.content ? lv_obj_get_height(s_widgets.content) : 0;
    if (metrics.width <= 0 && s_widgets.root)
    {
        metrics.width = lv_obj_get_width(s_widgets.root);
    }
    if (metrics.height <= 0 && s_widgets.root)
    {
        metrics.height = lv_obj_get_height(s_widgets.root) - profile.top_bar_height;
    }

    metrics.pad = profile.large_touch_hitbox ? 18 : (metrics.width < 360 ? 10 : 12);
    metrics.compact = metrics.width < 420;
    metrics.right_col_w = metrics.compact ? 122 : 176;
    if (metrics.width > 0)
    {
        const lv_coord_t max_right = metrics.width - (metrics.pad * 2) - 96;
        if (max_right < metrics.right_col_w)
        {
            metrics.right_col_w = max_right;
        }
    }
    if (metrics.right_col_w < 108)
    {
        metrics.right_col_w = 108;
    }
    metrics.right_x = metrics.width - metrics.pad - metrics.right_col_w;
    metrics.left_w = metrics.right_x - metrics.pad;
    if (metrics.left_w < 96)
    {
        metrics.left_w = 96;
    }

    metrics.focus_x = metrics.width / 2;
    metrics.focus_y = metrics.height / 2;

    metrics.info_top = metrics.compact ? (metrics.pad + 2) : (metrics.pad + 10);
    metrics.info_line_h = metrics.compact ? 12 : 14;
    metrics.info_gap = metrics.compact ? 1 : 3;
    metrics.zoom_size = metrics.compact ? 28 : 36;
    metrics.zoom_gap = metrics.compact ? 6 : 10;
    metrics.layer_w = metrics.compact ? 68 : 84;
    metrics.layer_h = metrics.compact ? 24 : 30;
    return metrics;
}

map_viewport::Model build_map_model(const map_viewport::GeoPoint& node_point,
                                    int zoom,
                                    const ViewMetrics& metrics,
                                    int pan_x,
                                    int pan_y)
{
    map_viewport::Model model{};
    model.focus_point = node_point;
    model.zoom = zoom;
    model.pan_x = pan_x + static_cast<int>(metrics.focus_x) - (metrics.width / 2);
    model.pan_y = pan_y + static_cast<int>(metrics.focus_y) - (metrics.height / 2);

    const auto& cfg = app::configFacade().getConfig();
    model.map_source = cfg.map_source;
    model.contour_enabled = cfg.map_contour_enabled;
    model.coord_system = cfg.map_coord_system;
    return model;
}

map_viewport::GeoPoint node_point_from_info(const chat::contacts::NodeInfo& node)
{
    map_viewport::GeoPoint point{};
    if (!node.position.valid)
    {
        return point;
    }

    point.valid = true;
    point.lat = static_cast<double>(node.position.latitude_i) * 1e-7;
    point.lon = static_cast<double>(node.position.longitude_i) * 1e-7;
    return point;
}

map_viewport::GeoPoint resolve_self_position()
{
    map_viewport::GeoPoint point{};

    if (app::hasAppFacade())
    {
        const chat::NodeId self_node_id = app::messagingFacade().getSelfNodeId();
        if (self_node_id != 0)
        {
            const auto* self_info = app::messagingFacade().getContactService().getNodeInfo(self_node_id);
            if (self_info && self_info->position.valid)
            {
                point.valid = true;
                point.lat = static_cast<double>(self_info->position.latitude_i) * 1e-7;
                point.lon = static_cast<double>(self_info->position.longitude_i) * 1e-7;
                return point;
            }
        }
    }

    const auto gps = platform::ui::gps::get_data();
    if (gps.valid)
    {
        point.valid = true;
        point.lat = gps.lat;
        point.lon = gps.lng;
    }
    return point;
}

bool center_tile_exists(const map_viewport::GeoPoint& node_point, int zoom, const ViewMetrics& metrics)
{
    if (!node_point.valid)
    {
        return false;
    }

    return map_viewport::focus_tile_available(build_map_model(node_point, zoom, metrics, 0, 0));
}

int best_available_zoom(const map_viewport::GeoPoint& node_point, const ViewMetrics& metrics)
{
    if (!node_point.valid)
    {
        return map_viewport::kDefaultZoom;
    }

    if (center_tile_exists(node_point, map_viewport::kDefaultZoom, metrics))
    {
        return map_viewport::kDefaultZoom;
    }

    for (int delta = 1; delta <= (map_viewport::kMaxZoom - map_viewport::kMinZoom); ++delta)
    {
        const int higher = map_viewport::kDefaultZoom + delta;
        if (higher <= map_viewport::kMaxZoom && center_tile_exists(node_point, higher, metrics))
        {
            return higher;
        }
        const int lower = map_viewport::kDefaultZoom - delta;
        if (lower >= map_viewport::kMinZoom && center_tile_exists(node_point, lower, metrics))
        {
            return lower;
        }
    }

    return map_viewport::kDefaultZoom;
}

int compute_initial_zoom(const map_viewport::GeoPoint& node_point,
                         const map_viewport::GeoPoint& self_point,
                         const ViewMetrics& metrics)
{
    if (!node_point.valid)
    {
        return map_viewport::kDefaultZoom;
    }
    if (!self_point.valid)
    {
        return best_available_zoom(node_point, metrics);
    }

    const double min_x = static_cast<double>(metrics.pad + 8);
    const double max_x = static_cast<double>(metrics.right_x - 14);
    const double min_y = static_cast<double>(metrics.pad + 6);
    const double max_y = static_cast<double>(metrics.height - metrics.pad - 6);

    for (int zoom = map_viewport::kMaxZoom; zoom >= map_viewport::kMinZoom; --zoom)
    {
        if (!center_tile_exists(node_point, zoom, metrics))
        {
            continue;
        }

        lv_point_t projected_self{};
        if (!map_viewport::preview_project_point(
                s_widgets.tile_layer, build_map_model(node_point, zoom, metrics, 0, 0), self_point, projected_self))
        {
            continue;
        }

        const double draw_x = static_cast<double>(projected_self.x);
        const double draw_y = static_cast<double>(projected_self.y);
        if (draw_x >= min_x && draw_x <= max_x && draw_y >= min_y && draw_y <= max_y)
        {
            return zoom;
        }
    }

    return best_available_zoom(node_point, metrics);
}

int select_zoom_after_delta(int current_zoom, int delta)
{
    return clamp_coord(current_zoom + delta, map_viewport::kMinZoom, map_viewport::kMaxZoom);
}

void set_hidden(lv_obj_t* obj, bool hidden)
{
    if (!obj)
    {
        return;
    }
    if (hidden)
    {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void format_node_id(uint32_t node_id, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    if (node_id <= 0xFFFFFFUL)
    {
        std::snprintf(out, out_len, "ID !%06lX", static_cast<unsigned long>(node_id));
    }
    else
    {
        std::snprintf(out, out_len, "ID !%08lX", static_cast<unsigned long>(node_id));
    }
}

void format_age_short(uint32_t ts, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    if (ts == 0)
    {
        std::snprintf(out, out_len, "Seen --");
        return;
    }

    const uint32_t now = sys::epoch_seconds_now();
    if (now <= ts)
    {
        std::snprintf(out, out_len, "Seen 0s");
        return;
    }

    const uint32_t age = now - ts;
    if (age < 60)
    {
        std::snprintf(out, out_len, "Seen %us", static_cast<unsigned>(age));
        return;
    }
    if (age < 3600)
    {
        std::snprintf(out, out_len, "Seen %um", static_cast<unsigned>(age / 60));
        return;
    }
    if (age < 86400)
    {
        std::snprintf(out, out_len, "Seen %uh", static_cast<unsigned>(age / 3600));
        return;
    }
    std::snprintf(out, out_len, "Seen %ud", static_cast<unsigned>(age / 86400));
}

[[maybe_unused]] double compute_accuracy_m(const chat::contacts::NodePosition& pos)
{
    if (pos.gps_accuracy_mm == 0)
    {
        return -1.0;
    }
    double acc = static_cast<double>(pos.gps_accuracy_mm) / 1000.0;
    const uint32_t dop = pos.pdop ? pos.pdop : (pos.hdop ? pos.hdop : pos.vdop);
    if (dop > 0)
    {
        acc *= static_cast<double>(dop) / 100.0;
    }
    return acc;
}

[[maybe_unused]] const char* protocol_name(chat::contacts::NodeProtocolType protocol)
{
    switch (protocol)
    {
    case chat::contacts::NodeProtocolType::Meshtastic:
        return "Meshtastic";
    case chat::contacts::NodeProtocolType::MeshCore:
        return "MeshCore";
    case chat::contacts::NodeProtocolType::RNode:
        return "RNode";
    case chat::contacts::NodeProtocolType::LXMF:
        return "LXMF";
    default:
        return "Unknown";
    }
}

std::string preferred_node_title(const chat::contacts::NodeInfo& node)
{
    if (!node.display_name.empty())
    {
        return node.display_name;
    }
    if (node.short_name[0] != '\0')
    {
        return node.short_name;
    }
    if (node.long_name[0] != '\0')
    {
        return node.long_name;
    }
    return ::ui::i18n::tr("NODE INFO");
}

void set_top_bar_title(const chat::contacts::NodeInfo& node)
{
    if (!s_top_bar.container)
    {
        return;
    }

    const std::string title = preferred_node_title(node);
    ::ui::widgets::top_bar_set_title(s_top_bar, title.c_str());
}

void format_coord_label(const char* prefix, double value, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s %.5f", prefix, value);
}

bool append_info_line(std::size_t& count, const char* text, lv_color_t color)
{
    if (!text || text[0] == '\0' || count >= kNodeInfoInfoLineCount)
    {
        return false;
    }

    set_label_text(s_widgets.info_labels[count], text);
    lv_obj_set_style_text_color(s_widgets.info_labels[count], color, 0);
    set_hidden(s_widgets.info_labels[count], false);
    NODE_INFO_LOG("append_info_line index=%u text='%s'\n",
                  static_cast<unsigned>(count),
                  text);
    ++count;
    return true;
}

void hide_unused_info_lines(std::size_t visible_count)
{
    for (std::size_t index = visible_count; index < kNodeInfoInfoLineCount; ++index)
    {
        set_hidden(s_widgets.info_labels[index], true);
    }
}

void build_protocol_line(const chat::contacts::NodeInfo& node, char* out, size_t out_len)
{
    std::snprintf(out, out_len, "%s", protocol_name(node.protocol));
}

bool build_rssi_line(const chat::contacts::NodeInfo& node, char* out, size_t out_len)
{
    if (std::isnan(node.rssi))
    {
        return false;
    }
    std::snprintf(out, out_len, "RSSI %.0f dBm", node.rssi);
    return true;
}

bool build_snr_line(const chat::contacts::NodeInfo& node, char* out, size_t out_len)
{
    if (std::isnan(node.snr))
    {
        return false;
    }
    std::snprintf(out, out_len, "SNR %+0.1f dB", node.snr);
    return true;
}

bool build_seen_line(const chat::contacts::NodeInfo& node, char* out, size_t out_len)
{
    if (node.last_seen == 0)
    {
        return false;
    }
    format_age_short(node.last_seen, out, out_len);
    return true;
}

void update_zoom_status_label(bool visible)
{
    if (!s_widgets.zoom_status_label)
    {
        return;
    }

    if (!visible)
    {
        set_label_text(s_widgets.zoom_status_label, "");
        set_hidden(s_widgets.zoom_status_label, true);
        return;
    }

    char zoom_buf[24];
    std::snprintf(zoom_buf, sizeof(zoom_buf), "Zoom %d", s_state.zoom);
    set_label_text(s_widgets.zoom_status_label, zoom_buf);
    lv_obj_set_style_text_color(s_widgets.zoom_status_label, kColorReadoutZoom, 0);
    set_hidden(s_widgets.zoom_status_label, false);
}

void update_overlay_text()
{
    if (!s_state.has_node)
    {
        NODE_INFO_LOG("update_overlay_text skipped: no node\n");
        return;
    }

    const auto& node = s_state.node;
    const map_viewport::GeoPoint node_point = node_point_from_info(node);
    const ViewMetrics metrics = view_metrics();
    log_node_summary("update_overlay_text", node);
    log_geo_point("update_overlay_text", "node_point", node_point);
    log_geo_point("update_overlay_text", "self_point", s_state.self_point);
    log_view_metrics("update_overlay_text", metrics);

    char id_buf[24];
    format_node_id(node.node_id, id_buf, sizeof(id_buf));
    set_label_text(s_widgets.id_label, id_buf);

    char coord_buf[40];
    if (node_point.valid)
    {
        format_coord_label("LON", node_point.lon, coord_buf, sizeof(coord_buf));
        set_label_text(s_widgets.lon_label, coord_buf);
        format_coord_label("LAT", node_point.lat, coord_buf, sizeof(coord_buf));
        set_label_text(s_widgets.lat_label, coord_buf);
        set_hidden(s_widgets.lon_label, false);
        set_hidden(s_widgets.lat_label, false);
    }
    else
    {
        set_label_text(s_widgets.lon_label, "");
        set_label_text(s_widgets.lat_label, "");
        set_hidden(s_widgets.lon_label, true);
        set_hidden(s_widgets.lat_label, true);
    }

    set_top_bar_title(node);

    std::size_t line_count = 0;
    char line[128];
    build_protocol_line(node, line, sizeof(line));
    append_info_line(line_count, line, kColorReadoutProtocol);

    if (build_rssi_line(node, line, sizeof(line)))
    {
        append_info_line(line_count, line, kColorReadoutRssi);
    }

    if (build_snr_line(node, line, sizeof(line)))
    {
        append_info_line(line_count, line, kColorReadoutSnr);
    }

    if (build_seen_line(node, line, sizeof(line)))
    {
        append_info_line(line_count, line, kColorReadoutSeen);
    }

    hide_unused_info_lines(line_count);
    update_zoom_status_label(node_point.valid);
    NODE_INFO_LOG("update_overlay_text done line_count=%u title='%s' id='%s' lon='%s' lat='%s'\n",
                  static_cast<unsigned>(line_count),
                  s_widgets.title_label ? lv_label_get_text(s_widgets.title_label) : "<null>",
                  s_widgets.id_label ? lv_label_get_text(s_widgets.id_label) : "<null>",
                  s_widgets.lon_label ? lv_label_get_text(s_widgets.lon_label) : "<null>",
                  s_widgets.lat_label ? lv_label_get_text(s_widgets.lat_label) : "<null>");
}

void position_overlay_widgets()
{
    const ViewMetrics metrics = view_metrics();
    if (!s_widgets.map_stage)
    {
        NODE_INFO_LOG("position_overlay_widgets skipped: map_stage missing\n");
        return;
    }

    log_view_metrics("position_overlay_widgets", metrics);

    map_viewport::set_size(s_state.viewport, metrics.width, metrics.height);

    lv_obj_set_pos(s_widgets.id_label, metrics.pad, metrics.pad);
    lv_obj_set_size(s_widgets.id_label, metrics.left_w, LV_SIZE_CONTENT);

    const lv_coord_t layer_x = (metrics.width - metrics.layer_w) / 2;
    const lv_coord_t layer_y = metrics.height - metrics.pad - metrics.layer_h;
    const lv_coord_t coord_label_w =
        clamp_coord(layer_x - metrics.pad - 10, 92, metrics.left_w);

    lv_obj_set_pos(s_widgets.lon_label, metrics.pad, metrics.height - metrics.pad - 28);
    lv_obj_set_size(s_widgets.lon_label, coord_label_w, LV_SIZE_CONTENT);

    lv_obj_set_pos(s_widgets.lat_label, metrics.pad, metrics.height - metrics.pad - 14);
    lv_obj_set_size(s_widgets.lat_label, coord_label_w, LV_SIZE_CONTENT);

    lv_obj_t* info_items[kNodeInfoInfoLineCount + 1]{};
    lv_coord_t info_item_widths[kNodeInfoInfoLineCount + 1]{};
    lv_coord_t info_item_heights[kNodeInfoInfoLineCount + 1]{};
    std::size_t visible_info_items = 0;
    auto collect_info_item = [&](lv_obj_t* item)
    {
        if (!item || is_hidden(item))
        {
            return;
        }

        lv_obj_set_size(item, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_update_layout(item);
        lv_coord_t width = lv_obj_get_width(item);
        if (width > metrics.right_col_w)
        {
            lv_obj_set_size(item, metrics.right_col_w, LV_SIZE_CONTENT);
            lv_obj_update_layout(item);
            width = lv_obj_get_width(item);
        }

        info_items[visible_info_items] = item;
        info_item_widths[visible_info_items] = width;
        info_item_heights[visible_info_items] = lv_obj_get_height(item);
        ++visible_info_items;
    };

    for (std::size_t index = 0; index < kNodeInfoInfoLineCount; ++index)
    {
        collect_info_item(s_widgets.info_labels[index]);
    }
    collect_info_item(s_widgets.zoom_status_label);

    if (s_widgets.info_panel)
    {
        if (visible_info_items == 0)
        {
            set_hidden(s_widgets.info_panel, true);
        }
        else
        {
            lv_coord_t max_item_width = 0;
            lv_coord_t content_height = 0;
            for (std::size_t index = 0; index < visible_info_items; ++index)
            {
                max_item_width = std::max(max_item_width, info_item_widths[index]);
                content_height += info_item_heights[index];
                if (index + 1 < visible_info_items)
                {
                    content_height += metrics.info_gap;
                }
            }

            const lv_coord_t panel_w = max_item_width + (kInfoPanelPadX * 2);
            const lv_coord_t panel_h = content_height + (kInfoPanelPadY * 2);
            const lv_coord_t panel_x = metrics.right_x + metrics.right_col_w - panel_w;
            const lv_coord_t panel_y = metrics.info_top - kInfoPanelPadY;
            lv_obj_set_pos(s_widgets.info_panel, panel_x, panel_y);
            lv_obj_set_size(s_widgets.info_panel, panel_w, panel_h);
            set_hidden(s_widgets.info_panel, false);

            lv_coord_t cursor_y = kInfoPanelPadY;
            for (std::size_t index = 0; index < visible_info_items; ++index)
            {
                lv_obj_set_pos(info_items[index],
                               panel_w - kInfoPanelPadX - info_item_widths[index],
                               cursor_y);
                lv_obj_set_size(info_items[index], info_item_widths[index], info_item_heights[index]);
                cursor_y += info_item_heights[index] + metrics.info_gap;
            }

            NODE_INFO_LOG(
                "position_overlay_widgets info_panel visible_items=%u pos=(%d,%d) size=%dx%d content=%dx%d\n",
                static_cast<unsigned>(visible_info_items),
                static_cast<int>(panel_x),
                static_cast<int>(panel_y),
                static_cast<int>(panel_w),
                static_cast<int>(panel_h),
                static_cast<int>(max_item_width),
                static_cast<int>(content_height));
        }
    }

    lv_obj_set_pos(s_widgets.no_position_label, metrics.pad, 0);
    lv_obj_set_size(s_widgets.no_position_label, metrics.left_w, LV_SIZE_CONTENT);
    lv_obj_update_layout(s_widgets.no_position_label);
    const lv_coord_t no_pos_w = lv_obj_get_width(s_widgets.no_position_label);
    const lv_coord_t no_pos_h = lv_obj_get_height(s_widgets.no_position_label);
    lv_obj_set_pos(s_widgets.no_position_label,
                   metrics.pad + ((metrics.left_w - no_pos_w) / 2),
                   (metrics.height - no_pos_h) / 2);

    const lv_coord_t zoom_x = metrics.width - metrics.pad - metrics.zoom_size;
    const lv_coord_t zoom_out_y = metrics.height - metrics.pad - metrics.zoom_size;
    const lv_coord_t zoom_in_y = zoom_out_y - metrics.zoom_size - metrics.zoom_gap;
    lv_obj_set_size(s_widgets.zoom_in_btn, metrics.zoom_size, metrics.zoom_size);
    lv_obj_set_size(s_widgets.zoom_out_btn, metrics.zoom_size, metrics.zoom_size);
    lv_obj_set_pos(s_widgets.zoom_in_btn, zoom_x, zoom_in_y);
    lv_obj_set_pos(s_widgets.zoom_out_btn, zoom_x, zoom_out_y);
    lv_obj_center(s_widgets.zoom_in_label);
    lv_obj_center(s_widgets.zoom_out_label);

    apply_layer_button_style(s_widgets.layer_btn, s_widgets.layer_label, metrics.compact);
    lv_obj_set_size(s_widgets.layer_btn, metrics.layer_w, metrics.layer_h);
    lv_obj_set_pos(s_widgets.layer_btn, layer_x, layer_y);
    lv_obj_center(s_widgets.layer_label);
    log_scene_widgets("position_overlay_widgets");
}

void position_circle_center(lv_obj_t* obj, lv_coord_t center_x, lv_coord_t center_y)
{
    if (!obj)
    {
        return;
    }
    lv_obj_set_pos(obj,
                   center_x - (lv_obj_get_width(obj) / 2),
                   center_y - (lv_obj_get_height(obj) / 2));
}

void apply_current_map_view(const map_viewport::GeoPoint& node_point, const ViewMetrics& metrics)
{
    if (s_state.map_ready && node_point.valid)
    {
        map_viewport::apply_model(
            s_state.viewport, build_map_model(node_point, s_state.zoom, metrics, s_state.pan_x, s_state.pan_y));
    }
    else
    {
        map_viewport::clear(s_state.viewport);
    }

    render_connection_and_markers(node_point, metrics);
}

void render_connection_and_markers(const map_viewport::GeoPoint& node_point, const ViewMetrics& metrics)
{
    if (!node_point.valid)
    {
        NODE_INFO_LOG("render_connection_and_markers: node invalid -> hide all markers\n");
        set_hidden(s_widgets.connection_line, true);
        set_hidden(s_widgets.marker_node_ring, true);
        set_hidden(s_widgets.marker_node_dot, true);
        set_hidden(s_widgets.marker_self_ring, true);
        set_hidden(s_widgets.marker_self_dot, true);
        set_hidden(s_widgets.distance_label, true);
        return;
    }

    lv_point_t node_screen{};
    if (!map_viewport::project_point(s_state.viewport, node_point, node_screen))
    {
        NODE_INFO_LOG("render_connection_and_markers: node projection failed\n");
        set_hidden(s_widgets.connection_line, true);
        set_hidden(s_widgets.marker_node_ring, true);
        set_hidden(s_widgets.marker_node_dot, true);
        set_hidden(s_widgets.marker_self_ring, true);
        set_hidden(s_widgets.marker_self_dot, true);
        set_hidden(s_widgets.distance_label, true);
        return;
    }

    position_circle_center(s_widgets.marker_node_ring, node_screen.x, node_screen.y);
    position_circle_center(s_widgets.marker_node_dot, node_screen.x, node_screen.y);
    set_hidden(s_widgets.marker_node_ring, false);
    set_hidden(s_widgets.marker_node_dot, false);

    if (!s_state.self_point.valid)
    {
        NODE_INFO_LOG("render_connection_and_markers: self position invalid -> node marker only\n");
        set_hidden(s_widgets.connection_line, true);
        set_hidden(s_widgets.marker_self_ring, true);
        set_hidden(s_widgets.marker_self_dot, true);
        set_hidden(s_widgets.distance_label, true);
        return;
    }

    lv_point_t self_screen{};
    if (!map_viewport::project_point(s_state.viewport, s_state.self_point, self_screen))
    {
        NODE_INFO_LOG("render_connection_and_markers: self projection failed -> node marker only\n");
        set_hidden(s_widgets.connection_line, true);
        set_hidden(s_widgets.marker_self_ring, true);
        set_hidden(s_widgets.marker_self_dot, true);
        set_hidden(s_widgets.distance_label, true);
        return;
    }

    const lv_coord_t draw_x = static_cast<lv_coord_t>(std::lround(
        clamp_double(static_cast<double>(self_screen.x),
                     static_cast<double>(metrics.pad + 6),
                     static_cast<double>(metrics.right_x - 16))));
    const lv_coord_t draw_y = static_cast<lv_coord_t>(std::lround(
        clamp_double(static_cast<double>(self_screen.y),
                     static_cast<double>(metrics.pad + 6),
                     static_cast<double>(metrics.height - metrics.pad - 6))));

    s_state.link_points[0].x = static_cast<float>(node_screen.x);
    s_state.link_points[0].y = static_cast<float>(node_screen.y);
    s_state.link_points[1].x = static_cast<float>(draw_x);
    s_state.link_points[1].y = static_cast<float>(draw_y);
    lv_line_set_points(s_widgets.connection_line, s_state.link_points, 2);
    lv_obj_set_pos(s_widgets.connection_line, 0, 0);
    set_hidden(s_widgets.connection_line, false);

    position_circle_center(s_widgets.marker_self_ring, draw_x, draw_y);
    position_circle_center(s_widgets.marker_self_dot, draw_x, draw_y);
    set_hidden(s_widgets.marker_self_ring, false);
    set_hidden(s_widgets.marker_self_dot, false);

    char distance_buf[24];
    dashboard::format_distance(
        dashboard::haversine_m(node_point.lat, node_point.lon, s_state.self_point.lat, s_state.self_point.lon),
        distance_buf,
        sizeof(distance_buf));
    const float bearing = dashboard::bearing_between(
        s_state.self_point.lat, s_state.self_point.lon, node_point.lat, node_point.lon);
    char distance_text[40];
    std::snprintf(distance_text,
                  sizeof(distance_text),
                  "%s / %s",
                  distance_buf,
                  dashboard::compass_rose(bearing));
    set_label_text(s_widgets.distance_label, distance_text);
    lv_obj_update_layout(s_widgets.distance_label);

    lv_coord_t label_x = static_cast<lv_coord_t>((node_screen.x + draw_x) / 2);
    lv_coord_t label_y = static_cast<lv_coord_t>((node_screen.y + draw_y) / 2) - 14;
    if (std::abs(draw_x - node_screen.x) < 22 && std::abs(draw_y - node_screen.y) < 22)
    {
        label_y -= 18;
    }

    const lv_coord_t label_w = lv_obj_get_width(s_widgets.distance_label);
    const lv_coord_t label_h = lv_obj_get_height(s_widgets.distance_label);
    label_x = clamp_coord(label_x - (label_w / 2), metrics.pad + 2, metrics.right_x - label_w - 10);
    label_y = clamp_coord(label_y - (label_h / 2), metrics.pad + 2, metrics.height - label_h - metrics.pad);
    lv_obj_set_pos(s_widgets.distance_label, label_x, label_y);
    set_hidden(s_widgets.distance_label, false);
    NODE_INFO_LOG("render_connection_and_markers: node_center=(%d,%d) self=(%d,%d) distance='%s' label=(%d,%d)\n",
                  static_cast<int>(node_screen.x),
                  static_cast<int>(node_screen.y),
                  static_cast<int>(draw_x),
                  static_cast<int>(draw_y),
                  s_widgets.distance_label ? lv_label_get_text(s_widgets.distance_label) : "<null>",
                  static_cast<int>(label_x),
                  static_cast<int>(label_y));
}

void update_gesture_availability(bool enabled)
{
    const bool active = enabled && s_state.map_ready && !is_layer_popup_open();
    map_viewport::set_gesture_enabled(s_state.viewport, active);
    NODE_INFO_LOG("update_gesture_availability enabled=%d popup=%d active=%d\n",
                  enabled ? 1 : 0,
                  is_layer_popup_open() ? 1 : 0,
                  active ? 1 : 0);
}

void on_map_gesture(const map_viewport::GestureEvent& event, void* /*user_data*/)
{
    if (!s_state.has_node || !s_state.map_ready || is_layer_popup_open())
    {
        return;
    }

    const map_viewport::GeoPoint node_point = node_point_from_info(s_state.node);
    if (!node_point.valid)
    {
        return;
    }

    switch (event.phase)
    {
    case map_viewport::GesturePhase::DragBegin:
        s_state.drag_start_pan_x = s_state.pan_x;
        s_state.drag_start_pan_y = s_state.pan_y;
        NODE_INFO_LOG("map_drag begin point=(%d,%d) pan=%d,%d\n",
                      static_cast<int>(event.point.x),
                      static_cast<int>(event.point.y),
                      s_state.pan_x,
                      s_state.pan_y);
        break;

    case map_viewport::GesturePhase::DragUpdate:
    {
        const int next_pan_x = s_state.drag_start_pan_x + event.total_dx;
        const int next_pan_y = s_state.drag_start_pan_y + event.total_dy;
        if (next_pan_x == s_state.pan_x && next_pan_y == s_state.pan_y)
        {
            return;
        }

        s_state.pan_x = next_pan_x;
        s_state.pan_y = next_pan_y;
        const ViewMetrics metrics = view_metrics();
        apply_current_map_view(node_point, metrics);
        update_zoom_status_label(true);
        NODE_INFO_LOG("map_drag update pan=%d,%d total_dx=%d total_dy=%d\n",
                      s_state.pan_x,
                      s_state.pan_y,
                      event.total_dx,
                      event.total_dy);
        break;
    }

    case map_viewport::GesturePhase::DragEnd:
    case map_viewport::GesturePhase::Cancel:
        NODE_INFO_LOG("map_drag end phase=%d pan=%d,%d total_dx=%d total_dy=%d\n",
                      static_cast<int>(event.phase),
                      s_state.pan_x,
                      s_state.pan_y,
                      event.total_dx,
                      event.total_dy);
        break;

    default:
        break;
    }
}

void update_zoom_button_state(bool enabled)
{
    if (!s_widgets.zoom_in_btn || !s_widgets.zoom_out_btn)
    {
        NODE_INFO_LOG("update_zoom_button_state skipped: buttons missing\n");
        return;
    }

    const bool enable_zoom_in = enabled && s_state.zoom < map_viewport::kMaxZoom;
    const bool enable_zoom_out = enabled && s_state.zoom > map_viewport::kMinZoom;

    if (enable_zoom_in)
    {
        lv_obj_clear_state(s_widgets.zoom_in_btn, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(s_widgets.zoom_in_btn, LV_STATE_DISABLED);
    }

    if (enable_zoom_out)
    {
        lv_obj_clear_state(s_widgets.zoom_out_btn, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(s_widgets.zoom_out_btn, LV_STATE_DISABLED);
    }

    NODE_INFO_LOG("update_zoom_button_state enabled=%d zoom=%d min=%d max=%d zoom_in_disabled=%d zoom_out_disabled=%d\n",
                  enabled ? 1 : 0,
                  s_state.zoom,
                  map_viewport::kMinZoom,
                  map_viewport::kMaxZoom,
                  lv_obj_has_state(s_widgets.zoom_in_btn, LV_STATE_DISABLED) ? 1 : 0,
                  lv_obj_has_state(s_widgets.zoom_out_btn, LV_STATE_DISABLED) ? 1 : 0);
}

void render_scene()
{
    NODE_INFO_LOG("render_scene begin has_node=%d map_ready=%d zoom=%d\n",
                  s_state.has_node ? 1 : 0,
                  s_state.map_ready ? 1 : 0,
                  s_state.zoom);

    if (!s_state.has_node)
    {
        hide_unused_info_lines(0);
        update_zoom_status_label(false);
        if (s_widgets.info_panel)
        {
            set_hidden(s_widgets.info_panel, true);
        }
        position_overlay_widgets();
        map_viewport::clear(s_state.viewport);
        set_hidden(s_widgets.no_position_label, false);
        update_gesture_availability(false);
        update_zoom_button_state(false);
        NODE_INFO_LOG("render_scene end: no node -> no_position visible\n");
        return;
    }

    update_overlay_text();
    position_overlay_widgets();

    const map_viewport::GeoPoint node_point = node_point_from_info(s_state.node);
    const ViewMetrics metrics = view_metrics();
    log_geo_point("render_scene", "node_point", node_point);
    log_geo_point("render_scene", "self_point", s_state.self_point);
    log_view_metrics("render_scene", metrics);
    apply_current_map_view(node_point, metrics);

    const bool has_position = node_point.valid;
    set_hidden(s_widgets.no_position_label, has_position);
    if (!has_position)
    {
        set_label_text(s_widgets.no_position_label, ::ui::i18n::tr("No position available"));
        set_hidden(s_widgets.connection_line, true);
        set_hidden(s_widgets.marker_node_ring, true);
        set_hidden(s_widgets.marker_node_dot, true);
        set_hidden(s_widgets.marker_self_ring, true);
        set_hidden(s_widgets.marker_self_dot, true);
        set_hidden(s_widgets.distance_label, true);
    }

    update_zoom_status_label(has_position);
    update_gesture_availability(has_position);
    update_zoom_button_state(has_position);
    NODE_INFO_LOG("render_scene end has_position=%d no_position_hidden=%d distance_hidden=%d root_hidden=%d\n",
                  has_position ? 1 : 0,
                  is_hidden(s_widgets.no_position_label) ? 1 : 0,
                  is_hidden(s_widgets.distance_label) ? 1 : 0,
                  is_hidden(s_widgets.root) ? 1 : 0);
    log_scene_widgets("render_scene");
}

void render_map_async(void* /*user_data*/)
{
    if (!s_widgets.root || !lv_obj_is_valid(s_widgets.root) || !s_state.has_node)
    {
        NODE_INFO_LOG("render_map_async skipped root=%p valid=%d has_node=%d\n",
                      s_widgets.root,
                      (s_widgets.root && lv_obj_is_valid(s_widgets.root)) ? 1 : 0,
                      s_state.has_node ? 1 : 0);
        return;
    }

    lv_obj_update_layout(s_widgets.root);
    const map_viewport::GeoPoint node_point = node_point_from_info(s_state.node);
    const ViewMetrics metrics = view_metrics();
    log_geo_point("render_map_async", "node_point", node_point);
    log_geo_point("render_map_async", "self_point", s_state.self_point);
    log_view_metrics("render_map_async", metrics);
    s_state.zoom = compute_initial_zoom(node_point, s_state.self_point, metrics);
    s_state.pan_x = 0;
    s_state.pan_y = 0;
    s_state.map_ready = true;
    NODE_INFO_LOG("render_map_async computed zoom=%d\n", s_state.zoom);
    render_scene();
}

void on_zoom_button_clicked(lv_event_t* e)
{
    if (!s_state.has_node)
    {
        NODE_INFO_LOG("on_zoom_button_clicked ignored: no node\n");
        return;
    }

    const intptr_t delta = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    const map_viewport::GeoPoint node_point = node_point_from_info(s_state.node);
    if (!node_point.valid)
    {
        NODE_INFO_LOG("on_zoom_button_clicked ignored: no node position delta=%d\n",
                      static_cast<int>(delta));
        return;
    }

    const int next_zoom = select_zoom_after_delta(s_state.zoom, static_cast<int>(delta));
    if (next_zoom == s_state.zoom)
    {
        NODE_INFO_LOG("on_zoom_button_clicked no-op delta=%d current_zoom=%d\n",
                      static_cast<int>(delta),
                      s_state.zoom);
        return;
    }

    const bool next_center_tile_exists = center_tile_exists(node_point, next_zoom, view_metrics());
    NODE_INFO_LOG("on_zoom_button_clicked delta=%d zoom %d -> %d center_tile=%d\n",
                  static_cast<int>(delta),
                  s_state.zoom,
                  next_zoom,
                  next_center_tile_exists ? 1 : 0);
    s_state.zoom = next_zoom;
    s_state.pan_x = 0;
    s_state.pan_y = 0;
    render_scene();
}

} // namespace

NodeInfoWidgets create(lv_obj_t* parent)
{
    NODE_INFO_LOG("create begin parent=%p\n", parent);
    log_widget_box("create.parent", "parent", parent);
    s_widgets = NodeInfoWidgets{};
    s_state = NodeInfoRuntimeState{};
    s_state.zoom = map_viewport::kDefaultZoom;

    s_widgets.root = layout::create_root(parent);
    s_widgets.header = layout::create_header(s_widgets.root);
    s_widgets.content = layout::create_content(s_widgets.root);

    lv_obj_set_style_bg_color(s_widgets.root, kColorBackdrop, 0);
    lv_obj_set_style_bg_opa(s_widgets.root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_widgets.content, kColorBackdropAlt, 0);
    lv_obj_set_style_bg_opa(s_widgets.content, LV_OPA_COVER, 0);
    make_plain(s_widgets.root);
    make_plain(s_widgets.content);
    lv_obj_clear_flag(s_widgets.root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_widgets.root);

    ::ui::widgets::TopBarConfig cfg;
    ::ui::widgets::top_bar_init(s_top_bar, s_widgets.header, cfg);
    s_widgets.back_btn = s_top_bar.back_btn;
    s_widgets.title_label = s_top_bar.title_label;
    s_widgets.battery_label = s_top_bar.right_label;
    if (s_widgets.back_btn)
    {
        s_widgets.back_label = lv_obj_get_child(s_widgets.back_btn, 0);
    }
    ::ui::widgets::top_bar_set_title(s_top_bar, ::ui::i18n::tr("NODE INFO"));
    ui_update_top_bar_battery(s_top_bar);
    s_widgets.map_viewport = map_viewport::create(s_state.viewport, s_widgets.content);
    s_widgets.map_stage = s_widgets.map_viewport.root;
    s_widgets.tile_layer = s_widgets.map_viewport.tile_layer;
    s_widgets.map_overlay_layer = s_widgets.map_viewport.overlay_layer;
    s_widgets.map_gesture_surface = s_widgets.map_viewport.gesture_surface;
    lv_obj_set_style_bg_color(s_widgets.map_stage, ::ui::theme::map_bg(), 0);
    lv_obj_set_style_bg_opa(s_widgets.map_stage, LV_OPA_COVER, 0);
    make_plain(s_widgets.map_stage);
    map_viewport::set_gesture_callback(s_state.viewport, on_map_gesture, nullptr);
    map_viewport::set_gesture_enabled(s_state.viewport, false);

    s_widgets.connection_line = lv_line_create(s_widgets.map_overlay_layer);
    lv_obj_set_style_line_color(s_widgets.connection_line, kColorLink, 0);
    lv_obj_set_style_line_width(s_widgets.connection_line, 2, 0);
    lv_obj_set_style_line_rounded(s_widgets.connection_line, true, 0);
    set_hidden(s_widgets.connection_line, true);

    s_widgets.marker_node_ring = lv_obj_create(s_widgets.map_overlay_layer);
    s_widgets.marker_node_dot = lv_obj_create(s_widgets.map_overlay_layer);
    s_widgets.marker_self_ring = lv_obj_create(s_widgets.map_overlay_layer);
    s_widgets.marker_self_dot = lv_obj_create(s_widgets.map_overlay_layer);
    apply_marker_style(s_widgets.marker_node_ring, 22, kColorNodeMarker, false);
    apply_marker_style(s_widgets.marker_node_dot, 10, kColorNodeMarker, true);
    apply_marker_style(s_widgets.marker_self_ring, 18, kColorSelfMarker, false);
    apply_marker_style(s_widgets.marker_self_dot, 8, kColorSelfMarker, true);
    set_hidden(s_widgets.marker_node_ring, true);
    set_hidden(s_widgets.marker_node_dot, true);
    set_hidden(s_widgets.marker_self_ring, true);
    set_hidden(s_widgets.marker_self_dot, true);

    s_widgets.id_label = create_label(s_widgets.map_stage, "ID !000000", font_montserrat_14_safe(), kColorId);
    lv_label_set_long_mode(s_widgets.id_label, LV_LABEL_LONG_DOT);

    s_widgets.lon_label = create_label(s_widgets.map_stage, "", font_montserrat_12_safe(), kColorLon);
    s_widgets.lat_label = create_label(s_widgets.map_stage, "", font_montserrat_12_safe(), kColorLat);
    set_hidden(s_widgets.lon_label, true);
    set_hidden(s_widgets.lat_label, true);

    s_widgets.no_position_label =
        create_label(s_widgets.map_stage, "No position available", font_montserrat_14_safe(), kColorMuted);
    lv_obj_set_style_text_align(s_widgets.no_position_label, LV_TEXT_ALIGN_CENTER, 0);
    set_hidden(s_widgets.no_position_label, true);

    s_widgets.distance_label =
        create_label(s_widgets.map_overlay_layer, "", font_montserrat_12_safe(), kColorDistance);
    set_hidden(s_widgets.distance_label, true);

    s_widgets.info_panel = lv_obj_create(s_widgets.map_overlay_layer);
    apply_info_panel_style(s_widgets.info_panel);
    set_hidden(s_widgets.info_panel, true);

    for (std::size_t index = 0; index < kNodeInfoInfoLineCount; ++index)
    {
        s_widgets.info_labels[index] = create_label(
            s_widgets.info_panel,
            "",
            font_montserrat_12_safe(),
            kColorMuted);
        lv_label_set_long_mode(s_widgets.info_labels[index], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(s_widgets.info_labels[index], LV_TEXT_ALIGN_RIGHT, 0);
        set_hidden(s_widgets.info_labels[index], true);
    }

    s_widgets.zoom_in_btn = lv_btn_create(s_widgets.map_stage);
    s_widgets.zoom_in_label = lv_label_create(s_widgets.zoom_in_btn);
    lv_label_set_text(s_widgets.zoom_in_label, "+");
    s_widgets.zoom_out_btn = lv_btn_create(s_widgets.map_stage);
    s_widgets.zoom_out_label = lv_label_create(s_widgets.zoom_out_btn);
    lv_label_set_text(s_widgets.zoom_out_label, "-");
    s_widgets.zoom_status_label =
        create_label(s_widgets.info_panel, "", font_montserrat_12_safe(), kColorMuted);
    lv_obj_set_style_text_align(s_widgets.zoom_status_label, LV_TEXT_ALIGN_RIGHT, 0);
    set_hidden(s_widgets.zoom_status_label, true);
    s_widgets.layer_btn = lv_btn_create(s_widgets.map_stage);
    s_widgets.layer_label = lv_label_create(s_widgets.layer_btn);
    lv_label_set_text(s_widgets.layer_label, ::ui::i18n::tr("Layer"));
    apply_zoom_button_style(s_widgets.zoom_in_btn, s_widgets.zoom_in_label);
    apply_zoom_button_style(s_widgets.zoom_out_btn, s_widgets.zoom_out_label);
    apply_layer_button_style(s_widgets.layer_btn, s_widgets.layer_label, view_metrics().compact);
    lv_obj_add_event_cb(s_widgets.zoom_in_btn,
                        on_zoom_button_clicked,
                        LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(+1)));
    lv_obj_add_event_cb(s_widgets.zoom_out_btn,
                        on_zoom_button_clicked,
                        LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(-1)));
    lv_obj_add_event_cb(s_widgets.layer_btn, on_layer_button_clicked, LV_EVENT_CLICKED, nullptr);
    update_zoom_button_state(false);

    lv_obj_update_layout(s_widgets.root);
    position_overlay_widgets();
    log_scene_widgets("create.end");
    NODE_INFO_LOG("create end\n");

    return s_widgets;
}

void destroy()
{
    NODE_INFO_LOG("destroy begin root=%p valid=%d\n",
                  s_widgets.root,
                  (s_widgets.root && lv_obj_is_valid(s_widgets.root)) ? 1 : 0);
    close_layer_popup();
    if (s_layer_popup.group)
    {
        lv_group_del(s_layer_popup.group);
        s_layer_popup.group = nullptr;
    }
    s_layer_popup = LayerPopupState{};
    map_viewport::destroy(s_state.viewport);

    if (s_widgets.root && lv_obj_is_valid(s_widgets.root))
    {
        lv_obj_del(s_widgets.root);
    }

    s_widgets = NodeInfoWidgets{};
    s_state = NodeInfoRuntimeState{};
    s_top_bar = ::ui::widgets::TopBar{};
    NODE_INFO_LOG("destroy end\n");
}

const NodeInfoWidgets& widgets()
{
    return s_widgets;
}

void set_node_info(const chat::contacts::NodeInfo& node)
{
    NODE_INFO_LOG("set_node_info begin\n");
    log_node_summary("set_node_info", node);
    s_state.node = node;
    s_state.has_node = true;
    s_state.self_point = resolve_self_position();
    s_state.map_ready = false;
    s_state.zoom = map_viewport::kDefaultZoom;
    s_state.pan_x = 0;
    s_state.pan_y = 0;
    s_state.drag_start_pan_x = 0;
    s_state.drag_start_pan_y = 0;
    map_viewport::clear(s_state.viewport);
    log_geo_point("set_node_info", "self_point", s_state.self_point);
    render_scene();
    NODE_INFO_LOG("set_node_info schedule async render\n");
    lv_async_call(render_map_async, nullptr);
}

} // namespace ui
} // namespace node_info
