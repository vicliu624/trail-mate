#include "ui/screens/gps/gps_page_components.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "lvgl.h"
#include "platform/ui/device_runtime.h"
#include "ui/page/page_profile.h"
#include "ui/screens/gps/gps_modal.h"
#include "ui/screens/gps/gps_page_input.h"
#include "ui/screens/gps/gps_page_lifetime.h"
#include "ui/screens/gps/gps_page_map.h"
#include "ui/screens/gps/gps_page_styles.h"
#include "ui/screens/gps/gps_route_overlay.h"
#include "ui/screens/gps/gps_state.h"
#include "ui/ui_common.h"
#include "ui/widgets/map/map_tiles.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#define GPS_DEBUG 0
#if GPS_DEBUG
#define GPS_LOG(...) std::printf(__VA_ARGS__)
#else
#define GPS_LOG(...)
#endif

#define GPS_FLOW_LOG(...)         \
    do                            \
    {                             \
        std::printf(__VA_ARGS__); \
        std::fflush(stdout);      \
    } while (0)

extern GPSPageState g_gps_state;

using gps::ui::lifetime::is_alive;

// ============================================================================
// Loading Component
// ============================================================================

void show_loading()
{
    if (!gps_ui::kShowLoadingOverlay)
    {
        return;
    }
    if (!is_alive() || g_gps_state.loading_msgbox != NULL || g_gps_state.page == NULL)
    {
        return;
    }

    const auto loading_size = ::ui::page_profile::resolve_modal_size(150, 80, g_gps_state.page);
    g_gps_state.loading_msgbox = lv_obj_create(g_gps_state.page);
    lv_obj_set_size(g_gps_state.loading_msgbox, loading_size.width, loading_size.height);
    gps::ui::styles::apply_loading_box(g_gps_state.loading_msgbox);
    lv_obj_center(g_gps_state.loading_msgbox);

    lv_obj_t* loading_label = lv_label_create(g_gps_state.loading_msgbox);
    lv_label_set_text(loading_label, "Loading...");
    gps::ui::styles::apply_loading_label(loading_label);
    lv_obj_center(loading_label);
}

void hide_loading()
{
    if (!gps_ui::kShowLoadingOverlay)
    {
        g_gps_state.loading_msgbox = NULL;
        return;
    }
    if (g_gps_state.loading_msgbox != NULL)
    {
        lv_obj_del(g_gps_state.loading_msgbox);
        g_gps_state.loading_msgbox = NULL;
    }
}

// ============================================================================
// Toast Component
// ============================================================================

static void toast_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    if (!is_alive())
    {
        return;
    }
    hide_toast();
}

void show_toast(const char* message, uint32_t duration_ms)
{
    if (!is_alive())
    {
        return;
    }

    hide_toast();

    if (g_gps_state.page == NULL)
    {
        return;
    }

    const auto toast_size = ::ui::page_profile::resolve_modal_size(200, 60, g_gps_state.page);
    g_gps_state.toast_msgbox = lv_obj_create(g_gps_state.page);
    lv_obj_set_size(g_gps_state.toast_msgbox, toast_size.width, toast_size.height);
    gps::ui::styles::apply_toast_box(g_gps_state.toast_msgbox);
    lv_obj_center(g_gps_state.toast_msgbox);

    lv_obj_t* toast_label = lv_label_create(g_gps_state.toast_msgbox);
    lv_label_set_text(toast_label, message);
    gps::ui::styles::apply_toast_label(toast_label);
    lv_obj_center(toast_label);

    if (g_gps_state.toast_timer != NULL)
    {
        gps::ui::lifetime::remove_timer(g_gps_state.toast_timer);
        g_gps_state.toast_timer = nullptr;
    }

    g_gps_state.toast_timer = gps::ui::lifetime::add_timer(toast_timer_cb, duration_ms, NULL);
    if (g_gps_state.toast_timer)
    {
        lv_timer_set_repeat_count(g_gps_state.toast_timer, 1);
    }
}

void hide_toast()
{
    if (g_gps_state.toast_timer != NULL)
    {
        gps::ui::lifetime::remove_timer(g_gps_state.toast_timer);
        g_gps_state.toast_timer = nullptr;
    }

    if (g_gps_state.toast_msgbox != NULL)
    {
        lv_obj_del(g_gps_state.toast_msgbox);
        g_gps_state.toast_msgbox = NULL;
    }
}

// ============================================================================
// PanIndicator Component
// ============================================================================

void show_pan_h_indicator()
{
    if (!is_alive())
    {
        return;
    }

    GPS_LOG("[GPS] show_pan_h_indicator: called, pan_h_indicator=%p, map=%p\n",
            g_gps_state.pan_h_indicator, g_gps_state.map);
    if (g_gps_state.pan_h_indicator != NULL || g_gps_state.map == NULL)
    {
        GPS_LOG("[GPS] show_pan_h_indicator: early return (already shown or map not ready)\n");
        return;
    }

    g_gps_state.pan_h_indicator = lv_label_create(g_gps_state.map);

    char indicator_text[64];
    const char* left_arrow = "<";
    const char* right_arrow = ">";
    snprintf(indicator_text, sizeof(indicator_text), "%s ------------ %s", left_arrow, right_arrow);
    lv_label_set_text(g_gps_state.pan_h_indicator, indicator_text);

    gps::ui::styles::apply_indicator_label(g_gps_state.pan_h_indicator);
    lv_obj_align(g_gps_state.pan_h_indicator, LV_ALIGN_BOTTOM_MID, 0, -20);

    lv_obj_add_flag(g_gps_state.pan_h_indicator, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_gps_state.pan_h_indicator, LV_OBJ_FLAG_SCROLLABLE);

    set_control_id(g_gps_state.pan_h_indicator, ControlId::PanHIndicator);

    if (app_g != NULL)
    {
        lv_group_add_obj(app_g, g_gps_state.pan_h_indicator);
    }

    extern void pan_indicator_event_cb(lv_event_t * e);
    extern void on_ui_event(lv_event_t * e);
    lv_obj_add_event_cb(g_gps_state.pan_h_indicator, pan_indicator_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_h_indicator, on_ui_event, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_h_indicator, on_ui_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_h_indicator, on_ui_event, LV_EVENT_ROTARY, NULL);

    lv_obj_clear_flag(g_gps_state.pan_h_indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_gps_state.pan_h_indicator);
    fix_ui_elements_position();
    lv_obj_update_layout(g_gps_state.map);
    lv_obj_invalidate(g_gps_state.pan_h_indicator);
}

void hide_pan_h_indicator()
{
    if (g_gps_state.pan_h_indicator == NULL)
    {
        return;
    }

    if (app_g != NULL)
    {
        lv_group_remove_obj(g_gps_state.pan_h_indicator);
    }

    lv_obj_del(g_gps_state.pan_h_indicator);
    g_gps_state.pan_h_indicator = nullptr;
}

void show_pan_v_indicator()
{
    if (!is_alive())
    {
        return;
    }

    GPS_LOG("[GPS] show_pan_v_indicator: called, pan_v_indicator=%p, map=%p\n",
            g_gps_state.pan_v_indicator, g_gps_state.map);
    if (g_gps_state.pan_v_indicator != NULL || g_gps_state.map == NULL)
    {
        GPS_LOG("[GPS] show_pan_v_indicator: early return (already shown or map not ready)\n");
        return;
    }

    g_gps_state.pan_v_indicator = lv_label_create(g_gps_state.map);

    char indicator_text[64];
    const char* up_arrow = "^";
    const char* down_arrow = "v";
    snprintf(indicator_text, sizeof(indicator_text), "%s\n|\n|\n|\n|\n|\n%s", up_arrow, down_arrow);
    lv_label_set_text(g_gps_state.pan_v_indicator, indicator_text);

    gps::ui::styles::apply_indicator_label(g_gps_state.pan_v_indicator);
    lv_obj_align(g_gps_state.pan_v_indicator, LV_ALIGN_LEFT_MID, 20, 0);

    lv_obj_add_flag(g_gps_state.pan_v_indicator, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_gps_state.pan_v_indicator, LV_OBJ_FLAG_SCROLLABLE);

    set_control_id(g_gps_state.pan_v_indicator, ControlId::PanVIndicator);

    if (app_g != NULL)
    {
        lv_group_add_obj(app_g, g_gps_state.pan_v_indicator);
    }

    extern void pan_indicator_event_cb(lv_event_t * e);
    extern void on_ui_event(lv_event_t * e);
    lv_obj_add_event_cb(g_gps_state.pan_v_indicator, pan_indicator_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_v_indicator, on_ui_event, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_v_indicator, on_ui_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_v_indicator, on_ui_event, LV_EVENT_ROTARY, NULL);

    lv_obj_clear_flag(g_gps_state.pan_v_indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_gps_state.pan_v_indicator);
    fix_ui_elements_position();
    lv_obj_update_layout(g_gps_state.map);
    lv_obj_invalidate(g_gps_state.pan_v_indicator);
}

void hide_pan_v_indicator()
{
    if (g_gps_state.pan_v_indicator == NULL)
    {
        return;
    }

    if (app_g != NULL)
    {
        lv_group_remove_obj(g_gps_state.pan_v_indicator);
    }

    lv_obj_del(g_gps_state.pan_v_indicator);
    g_gps_state.pan_v_indicator = nullptr;
}

// ============================================================================
// ZoomModal Component
// ============================================================================

namespace
{
constexpr int kZoomLevelCount = gps_ui::kMaxZoom - gps_ui::kMinZoom + 1;
lv_obj_t* s_zoom_touch_level_btns[kZoomLevelCount] = {nullptr};

bool use_touch_zoom_popup()
{
    return modal_uses_touch_layout();
}

void update_zoom_touch_level_btn_selected(lv_obj_t* btn, bool selected)
{
    if (btn == nullptr)
    {
        return;
    }
    lv_obj_set_style_outline_width(btn, selected ? 2 : 0, LV_PART_MAIN);
    lv_obj_set_style_outline_color(btn, lv_color_hex(0x2F6FD6), LV_PART_MAIN);
    lv_obj_set_style_outline_pad(btn, 0, LV_PART_MAIN);
}

void reset_zoom_touch_level_btns()
{
    for (int index = 0; index < kZoomLevelCount; ++index)
    {
        s_zoom_touch_level_btns[index] = nullptr;
    }
}

void zoom_popup_sync_touch_selection_impl()
{
    if (!use_touch_zoom_popup())
    {
        return;
    }

    if (g_gps_state.popup_label != nullptr)
    {
        char summary[48];
        std::snprintf(summary, sizeof(summary), "Selected level: %d", g_gps_state.popup_zoom);
        lv_label_set_text(g_gps_state.popup_label, summary);
    }

    const int selected_index = g_gps_state.popup_zoom - gps_ui::kMinZoom;
    for (int index = 0; index < kZoomLevelCount; ++index)
    {
        lv_obj_t* btn = s_zoom_touch_level_btns[index];
        update_zoom_touch_level_btn_selected(btn, index == selected_index);
    }

    if (selected_index >= 0 && selected_index < kZoomLevelCount)
    {
        lv_obj_t* selected_btn = s_zoom_touch_level_btns[selected_index];
        if (selected_btn != nullptr)
        {
            lv_obj_scroll_to_view(selected_btn, LV_ANIM_OFF);
        }
    }
}

void zoom_popup_touch_level_btn_event_cb(lv_event_t* e)
{
    if (!is_alive() || lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    const int level = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (level < gps_ui::kMinZoom || level > gps_ui::kMaxZoom)
    {
        return;
    }

    g_gps_state.popup_zoom = level;
    zoom_popup_sync_widgets();
}

lv_coord_t zoom_popup_title_height()
{
    return modal_resolve_title_height();
}

lv_coord_t zoom_popup_action_button_height()
{
    return modal_resolve_action_button_height();
}

void resize_zoom_popup_window(lv_obj_t* win)
{
    if (!win || !use_touch_zoom_popup())
    {
        return;
    }

    const auto modal_size = ::ui::page_profile::resolve_modal_size(480, 520, lv_screen_active());
    lv_obj_set_size(win, modal_size.width, modal_size.height);
    lv_obj_center(win);
}

const char* zoom_popup_roller_options()
{
    return "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18";
}

void zoom_popup_roller_event_cb(lv_event_t* e)
{
    if (!is_alive() || lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
    {
        return;
    }

    lv_obj_t* target = lv_event_get_target_obj(e);
    if (target == nullptr || target != g_gps_state.popup_roller)
    {
        return;
    }

    g_gps_state.popup_zoom = gps_ui::kMinZoom + static_cast<int>(lv_roller_get_selected(target));
    zoom_popup_sync_widgets();
}

void zoom_popup_apply_btn_event_cb(lv_event_t* e)
{
    if (!is_alive() || lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    zoom_popup_apply_selection();
}

void zoom_popup_cancel_btn_event_cb(lv_event_t* e)
{
    if (!is_alive() || lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    hide_zoom_popup();
}

lv_obj_t* create_zoom_popup_action_button(lv_obj_t* parent, const char* text, lv_event_cb_t callback)
{
    return modal_create_touch_action_button(parent, text, callback, nullptr, LV_PCT(48));
}

} // namespace

void zoom_popup_sync_touch_selection()
{
    zoom_popup_sync_touch_selection_impl();
}

static void build_zoom_popup_ui(lv_obj_t* win)
{
    if (!is_alive() || !win)
    {
        return;
    }

    g_gps_state.popup_label = nullptr;
    g_gps_state.popup_roller = nullptr;
    g_gps_state.popup_apply_btn = nullptr;
    g_gps_state.popup_cancel_btn = nullptr;
    reset_zoom_touch_level_btns();

    resize_zoom_popup_window(win);
    gps::ui::styles::apply_zoom_popup_win(win);

    if (!use_touch_zoom_popup())
    {
        lv_obj_t* title_bar = lv_obj_create(win);
        lv_obj_set_size(title_bar, LV_PCT(100), 35);
        gps::ui::styles::apply_zoom_popup_title_bar(title_bar);
        lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t* title_label = lv_label_create(title_bar);
        lv_label_set_text(title_label, "select level");
        gps::ui::styles::apply_zoom_popup_title_label(title_label);
        lv_obj_center(title_label);

        lv_obj_t* content_area = lv_obj_create(win);
        lv_obj_set_size(content_area, LV_PCT(100), LV_PCT(100) - 35);
        gps::ui::styles::apply_zoom_popup_content_area(content_area);
        lv_obj_align(content_area, LV_ALIGN_BOTTOM_MID, 0, 0);

        g_gps_state.popup_label = lv_label_create(content_area);
        char zoom_text[32];
        std::snprintf(zoom_text, sizeof(zoom_text), "%d", g_gps_state.popup_zoom);
        lv_label_set_text(g_gps_state.popup_label, zoom_text);

        gps::ui::styles::apply_zoom_popup_value_label(g_gps_state.popup_label);
        lv_obj_center(g_gps_state.popup_label);

        lv_obj_add_flag(g_gps_state.popup_label, LV_OBJ_FLAG_CLICKABLE);
        return;
    }

    const lv_coord_t title_height = zoom_popup_title_height();

    lv_obj_t* title_bar = lv_obj_create(win);
    lv_obj_set_size(title_bar, LV_PCT(100), title_height);
    gps::ui::styles::apply_zoom_popup_title_bar(title_bar);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, "select level");
    gps::ui::styles::apply_zoom_popup_title_label(title_label);
    lv_obj_center(title_label);

    lv_obj_t* content_area = lv_obj_create(win);
    const lv_coord_t win_height = lv_obj_get_height(win);
    const lv_coord_t pad_top = lv_obj_get_style_pad_top(win, LV_PART_MAIN);
    const lv_coord_t pad_bottom = lv_obj_get_style_pad_bottom(win, LV_PART_MAIN);
    const lv_coord_t inner_height = win_height > (pad_top + pad_bottom)
                                        ? static_cast<lv_coord_t>(win_height - pad_top - pad_bottom)
                                        : 0;
    const lv_coord_t content_height = inner_height > title_height ? (inner_height - title_height) : 0;
    lv_obj_set_size(content_area, LV_PCT(100), content_height);
    gps::ui::styles::apply_zoom_popup_content_area(content_area);
    lv_obj_align(content_area, LV_ALIGN_BOTTOM_MID, 0, 0);

    if (use_touch_zoom_popup())
    {
        const lv_coord_t modal_pad = ::ui::page_profile::resolve_modal_pad();
        lv_obj_set_style_pad_all(content_area, modal_pad, 0);
        lv_obj_set_style_pad_row(content_area, 10, 0);
        lv_obj_clear_flag(content_area, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(content_area, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(content_area, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        g_gps_state.popup_label = lv_label_create(content_area);
        gps::ui::styles::apply_control_button_label(g_gps_state.popup_label);
        lv_obj_set_width(g_gps_state.popup_label, LV_PCT(100));
        lv_obj_set_style_text_align(g_gps_state.popup_label, LV_TEXT_ALIGN_LEFT, 0);

        lv_obj_t* level_list = lv_obj_create(content_area);
        gps::ui::styles::apply_tracker_modal_list(level_list);
        lv_obj_set_width(level_list, LV_PCT(100));
        lv_obj_set_height(level_list, 0);
        lv_obj_set_flex_grow(level_list, 1);
        lv_obj_set_style_pad_left(level_list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_right(level_list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_top(level_list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(level_list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(level_list, 6, LV_PART_MAIN);
        lv_obj_set_style_radius(level_list, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(level_list, 0, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(level_list, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_scroll_dir(level_list, LV_DIR_VER);
        lv_obj_set_flex_flow(level_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(level_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        const lv_coord_t level_row_height = zoom_popup_action_button_height() > 58
                                                ? zoom_popup_action_button_height()
                                                : 58;
        for (int level = gps_ui::kMinZoom; level <= gps_ui::kMaxZoom; ++level)
        {
            char level_text[24];
            std::snprintf(level_text, sizeof(level_text), "Level %d", level);

            lv_obj_t* btn = lv_btn_create(level_list);
            lv_obj_set_width(btn, LV_PCT(100));
            lv_obj_set_height(btn, level_row_height);
            lv_obj_set_style_pad_ver(btn, 10, LV_PART_MAIN);
            lv_obj_set_style_pad_hor(btn, 12, LV_PART_MAIN);
            gps::ui::styles::apply_control_button(btn);

            lv_obj_t* label = lv_label_create(btn);
            lv_label_set_text(label, level_text);
            gps::ui::styles::apply_control_button_label(label);
            lv_obj_set_width(label, LV_PCT(100));
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_center(label);

            lv_obj_add_event_cb(btn,
                                zoom_popup_touch_level_btn_event_cb,
                                LV_EVENT_CLICKED,
                                reinterpret_cast<void*>(static_cast<uintptr_t>(level)));
            s_zoom_touch_level_btns[level - gps_ui::kMinZoom] = btn;
        }

        lv_obj_t* action_row = lv_obj_create(content_area);
        lv_obj_remove_style_all(action_row);
        lv_obj_set_size(action_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(action_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(action_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(action_row, modal_pad, 0);
        lv_obj_set_style_pad_top(action_row, 6, 0);

        g_gps_state.popup_cancel_btn =
            create_zoom_popup_action_button(action_row, "Cancel", zoom_popup_cancel_btn_event_cb);
        g_gps_state.popup_apply_btn =
            create_zoom_popup_action_button(action_row, "Apply", zoom_popup_apply_btn_event_cb);
        return;
    }
}

void show_zoom_popup()
{
    if (!is_alive())
    {
        return;
    }

    if (!modal_open(g_gps_state.zoom_modal, lv_screen_active(), app_g))
    {
        return;
    }

    g_gps_state.popup_zoom = g_gps_state.zoom_level;

    build_zoom_popup_ui(g_gps_state.zoom_modal.win);
    zoom_popup_sync_widgets();

    if (g_gps_state.zoom_modal.group != NULL)
    {
        lv_obj_t* focus_target = g_gps_state.popup_roller != NULL ? g_gps_state.popup_roller
                                                                  : (g_gps_state.popup_apply_btn != NULL ? g_gps_state.popup_apply_btn
                                                                                                         : g_gps_state.popup_label);

        lv_group_remove_all_objs(g_gps_state.zoom_modal.group);
        if (g_gps_state.popup_roller != NULL)
        {
            lv_group_add_obj(g_gps_state.zoom_modal.group, g_gps_state.popup_roller);
        }
        if (g_gps_state.popup_apply_btn != NULL)
        {
            lv_group_add_obj(g_gps_state.zoom_modal.group, g_gps_state.popup_apply_btn);
        }
        if (g_gps_state.popup_cancel_btn != NULL)
        {
            lv_group_add_obj(g_gps_state.zoom_modal.group, g_gps_state.popup_cancel_btn);
        }
        if (g_gps_state.popup_label != NULL && g_gps_state.popup_roller == NULL && g_gps_state.popup_apply_btn == NULL)
        {
            lv_group_add_obj(g_gps_state.zoom_modal.group, g_gps_state.popup_label);
        }

        if (focus_target != NULL)
        {
            set_default_group(g_gps_state.zoom_modal.group);
            bind_navigation_inputs_to_group(g_gps_state.zoom_modal.group);
            lv_group_focus_obj(focus_target);
            lv_group_set_editing(g_gps_state.zoom_modal.group, true);
            lv_obj_invalidate(focus_target);
        }
    }

    if (g_gps_state.popup_roller != NULL)
    {
        set_control_id(g_gps_state.popup_roller, ControlId::ZoomValueLabel);
        lv_obj_add_event_cb(g_gps_state.popup_roller, on_ui_event, LV_EVENT_ROTARY, NULL);
        lv_obj_add_event_cb(g_gps_state.popup_roller, on_ui_event, LV_EVENT_KEY, NULL);
    }

    if (g_gps_state.popup_label != NULL)
    {
        set_control_id(g_gps_state.popup_label, ControlId::ZoomValueLabel);
        lv_obj_add_event_cb(g_gps_state.popup_label, on_ui_event, LV_EVENT_ROTARY, NULL);
        lv_obj_add_event_cb(g_gps_state.popup_label, on_ui_event, LV_EVENT_KEY, NULL);
    }

    if (!g_gps_state.zoom_win_cb_bound)
    {
        set_control_id(g_gps_state.zoom_modal.win, ControlId::ZoomWin);
        lv_obj_add_event_cb(g_gps_state.zoom_modal.win, on_ui_event, LV_EVENT_ROTARY, NULL);
        lv_obj_add_event_cb(g_gps_state.zoom_modal.win, on_ui_event, LV_EVENT_KEY, NULL);
        lv_obj_add_flag(g_gps_state.zoom_modal.win, LV_OBJ_FLAG_CLICKABLE);
        g_gps_state.zoom_win_cb_bound = true;
    }
}

void hide_zoom_popup()
{
    if (!g_gps_state.zoom_modal.is_open())
    {
        return;
    }

    modal_close(g_gps_state.zoom_modal);
    g_gps_state.popup_label = nullptr;
    g_gps_state.popup_roller = nullptr;
    g_gps_state.popup_apply_btn = nullptr;
    g_gps_state.popup_cancel_btn = nullptr;
    reset_zoom_touch_level_btns();

    if (app_g != NULL)
    {
        lv_group_set_editing(app_g, false);
        set_default_group(app_g);
        bind_navigation_inputs_to_group(app_g);

        if (g_gps_state.zoom != NULL)
        {
            lv_group_focus_obj(g_gps_state.zoom);
        }
    }
}

// ============================================================================
// LayerModal Component
// ============================================================================

namespace
{
lv_obj_t* s_layer_source_label = nullptr;
lv_obj_t* s_layer_contour_label = nullptr;
lv_obj_t* s_layer_source_btns[3] = {nullptr, nullptr, nullptr};
lv_obj_t* s_layer_contour_btn = nullptr;

void update_layer_btn_selected(lv_obj_t* btn, bool selected)
{
    if (btn == nullptr)
    {
        return;
    }
    lv_obj_set_style_outline_width(btn, selected ? 2 : 0, LV_PART_MAIN);
    lv_obj_set_style_outline_color(btn, lv_color_hex(0x2F6FD6), LV_PART_MAIN);
    lv_obj_set_style_outline_pad(btn, 0, LV_PART_MAIN);
}

void refresh_layer_popup_labels()
{
    uint8_t map_source = sanitize_map_source(app::configFacade().getConfig().map_source);
    bool contour = app::configFacade().getConfig().map_contour_enabled;

    if (s_layer_source_label != nullptr)
    {
        char text[48];
        snprintf(text, sizeof(text), "Base: %s", map_source_label(map_source));
        lv_label_set_text(s_layer_source_label, text);
    }
    if (s_layer_contour_label != nullptr)
    {
        lv_label_set_text(s_layer_contour_label, contour ? "Contour: ON" : "Contour: OFF");
    }
    for (uint8_t i = 0; i < 3; ++i)
    {
        update_layer_btn_selected(s_layer_source_btns[i], i == map_source);
    }
    update_layer_btn_selected(s_layer_contour_btn, contour);
    if (s_layer_contour_btn != nullptr)
    {
        lv_obj_t* label = lv_obj_get_child(s_layer_contour_btn, 0);
        if (label != nullptr)
        {
            lv_label_set_text(label, contour ? "Contour: ON" : "Contour: OFF");
        }
    }
}

void layer_set_map_source(uint8_t map_source)
{
    app::IAppConfigFacade& config_api = app::configFacade();
    uint8_t previous = sanitize_map_source(config_api.getConfig().map_source);
    uint8_t normalized = sanitize_map_source(map_source);
    if (config_api.getConfig().map_source != normalized)
    {
        GPS_FLOW_LOG("[GPS][MAP][flow] layer_source change from=%u to=%u contour=%d\n",
                     previous,
                     normalized,
                     config_api.getConfig().map_contour_enabled);
        config_api.getConfig().map_source = normalized;
        config_api.saveConfig();
        update_map_tiles(false);
        log_map_tile_state("layer_source");
    }

    if (!platform::ui::device::sd_ready())
    {
        show_toast("No SD Card", 1200);
    }
    else if (!map_source_directory_available(normalized))
    {
        char message[44];
        snprintf(message, sizeof(message), "%s layer missing", map_source_label(normalized));
        show_toast(message, 1600);
    }
    refresh_layer_popup_labels();
}

void layer_toggle_contour()
{
    app::IAppConfigFacade& config_api = app::configFacade();
    bool previous = config_api.getConfig().map_contour_enabled;
    bool enabled = !config_api.getConfig().map_contour_enabled;
    GPS_FLOW_LOG("[GPS][MAP][flow] contour toggle from=%d to=%d src=%u\n",
                 previous,
                 enabled,
                 sanitize_map_source(config_api.getConfig().map_source));
    config_api.getConfig().map_contour_enabled = enabled;
    config_api.saveConfig();
    update_map_tiles(false);
    log_map_tile_state("contour_toggle");

    if (enabled)
    {
        if (!platform::ui::device::sd_ready())
        {
            show_toast("No SD Card", 1200);
        }
        else if (!contour_directory_available())
        {
            show_toast("Contour data missing", 1600);
        }
    }

    refresh_layer_popup_labels();
}

void on_layer_source_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    uint8_t map_source = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    layer_set_map_source(map_source);
}

void on_layer_contour_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    layer_toggle_contour();
}

void on_layer_close_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    hide_layer_popup();
}

void on_layer_button_key(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_KEY)
    {
        return;
    }
    lv_key_t key = static_cast<lv_key_t>(lv_event_get_key(e));
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        hide_layer_popup();
    }
}

void on_layer_bg_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    lv_obj_t* target = lv_event_get_target_obj(e);
    if (target != g_gps_state.layer_modal.bg)
    {
        return;
    }
    hide_layer_popup();
}
} // namespace

void show_layer_popup()
{
    if (!is_alive())
    {
        return;
    }

    if (!modal_open(g_gps_state.layer_modal, lv_screen_active(), app_g))
    {
        return;
    }
    if (g_gps_state.layer_modal.bg != nullptr)
    {
        lv_obj_add_event_cb(g_gps_state.layer_modal.bg, on_layer_bg_clicked, LV_EVENT_CLICKED, nullptr);
    }

    const bool touch_layout = modal_uses_touch_layout();
    modal_set_requested_size(g_gps_state.layer_modal, touch_layout ? 480 : 280, touch_layout ? 560 : 210);

    lv_obj_t* win = g_gps_state.layer_modal.win;
    if (win == nullptr)
    {
        return;
    }

    auto create_action_btn = [&](lv_obj_t* parent, const char* text, lv_event_cb_t cb, uintptr_t user_data) -> lv_obj_t*
    {
        lv_obj_t* btn = nullptr;
        if (touch_layout)
        {
            btn = modal_create_touch_action_button(parent, text, cb, reinterpret_cast<void*>(user_data), LV_PCT(100));
        }
        else
        {
            btn = lv_btn_create(parent);
            lv_obj_set_size(btn, LV_PCT(100), 22);
            gps::ui::styles::apply_control_button(btn);
            lv_obj_t* label = lv_label_create(btn);
            lv_label_set_text(label, text);
            gps::ui::styles::apply_control_button_label(label);
            lv_obj_center(label);
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(user_data));
        }
        lv_obj_add_event_cb(btn, on_layer_button_key, LV_EVENT_KEY, nullptr);
        return btn;
    };

    lv_obj_t* osm_btn = nullptr;
    lv_obj_t* terrain_btn = nullptr;
    lv_obj_t* satellite_btn = nullptr;
    lv_obj_t* contour_btn = nullptr;
    lv_obj_t* close_btn = nullptr;

    if (touch_layout)
    {
        gps::ui::styles::apply_zoom_popup_win(win);
        lv_obj_t* title_bar = modal_create_touch_title_bar(win, "Map Layer");
        const lv_coord_t title_height = title_bar != nullptr ? lv_obj_get_height(title_bar) : modal_resolve_title_height();
        lv_obj_t* content = modal_create_touch_content_area(win, title_height);
        if (content == nullptr)
        {
            return;
        }
        lv_obj_set_style_pad_row(content, 10, 0);

        lv_obj_t* summary = lv_obj_create(content);
        lv_obj_set_width(summary, LV_PCT(100));
        lv_obj_set_height(summary, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(summary, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(summary, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(summary, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(summary, 4, LV_PART_MAIN);
        lv_obj_set_flex_flow(summary, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(summary, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(summary, LV_OBJ_FLAG_SCROLLABLE);

        s_layer_source_label = lv_label_create(summary);
        gps::ui::styles::apply_control_button_label(s_layer_source_label);
        lv_obj_set_width(s_layer_source_label, LV_PCT(100));
        lv_label_set_long_mode(s_layer_source_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(s_layer_source_label, LV_TEXT_ALIGN_LEFT, 0);

        s_layer_contour_label = lv_label_create(summary);
        gps::ui::styles::apply_control_button_label(s_layer_contour_label);
        lv_obj_set_width(s_layer_contour_label, LV_PCT(100));
        lv_label_set_long_mode(s_layer_contour_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(s_layer_contour_label, LV_TEXT_ALIGN_LEFT, 0);

        lv_obj_t* action_list = lv_obj_create(content);
        lv_obj_set_width(action_list, LV_PCT(100));
        lv_obj_set_height(action_list, 0);
        lv_obj_set_flex_grow(action_list, 1);
        lv_obj_set_style_bg_opa(action_list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(action_list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(action_list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(action_list, 10, LV_PART_MAIN);
        lv_obj_set_flex_flow(action_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(action_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_scroll_dir(action_list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(action_list, LV_SCROLLBAR_MODE_AUTO);

        osm_btn = create_action_btn(action_list, "OSM", on_layer_source_clicked, static_cast<uintptr_t>(0));
        terrain_btn = create_action_btn(action_list, "Terrain", on_layer_source_clicked, static_cast<uintptr_t>(1));
        satellite_btn = create_action_btn(action_list, "Satellite", on_layer_source_clicked, static_cast<uintptr_t>(2));
        contour_btn = create_action_btn(action_list, "Contour: OFF", on_layer_contour_clicked, static_cast<uintptr_t>(0));
        close_btn = create_action_btn(action_list, "Close", on_layer_close_clicked, static_cast<uintptr_t>(0));
    }
    else
    {
        lv_obj_t* title = lv_label_create(win);
        lv_label_set_text(title, "Map Layer");
        gps::ui::styles::apply_control_button_label(title);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t* summary = lv_obj_create(win);
        lv_obj_set_size(summary, LV_PCT(100), 24);
        lv_obj_align(summary, LV_ALIGN_TOP_MID, 0, 28);
        lv_obj_set_flex_flow(summary, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(summary, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(summary, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(summary, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_left(summary, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_right(summary, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_top(summary, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(summary, 0, LV_PART_MAIN);
        lv_obj_clear_flag(summary, LV_OBJ_FLAG_SCROLLABLE);

        s_layer_source_label = lv_label_create(summary);
        gps::ui::styles::apply_control_button_label(s_layer_source_label);

        s_layer_contour_label = lv_label_create(summary);
        gps::ui::styles::apply_control_button_label(s_layer_contour_label);

        lv_obj_t* list = lv_obj_create(win);
        lv_obj_set_size(list, LV_PCT(100), 126);
        lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(list, 2, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

        osm_btn = create_action_btn(list, "OSM", on_layer_source_clicked, static_cast<uintptr_t>(0));
        terrain_btn = create_action_btn(list, "Terrain", on_layer_source_clicked, static_cast<uintptr_t>(1));
        satellite_btn = create_action_btn(list, "Satellite", on_layer_source_clicked, static_cast<uintptr_t>(2));
        contour_btn = create_action_btn(list, "Contour: OFF", on_layer_contour_clicked, static_cast<uintptr_t>(0));
        close_btn = create_action_btn(list, "Cancel", on_layer_close_clicked, static_cast<uintptr_t>(0));
    }

    s_layer_source_btns[0] = osm_btn;
    s_layer_source_btns[1] = terrain_btn;
    s_layer_source_btns[2] = satellite_btn;
    s_layer_contour_btn = contour_btn;
    refresh_layer_popup_labels();

    if (g_gps_state.layer_modal.group != NULL)
    {
        lv_group_remove_all_objs(g_gps_state.layer_modal.group);
        lv_group_add_obj(g_gps_state.layer_modal.group, osm_btn);
        lv_group_add_obj(g_gps_state.layer_modal.group, terrain_btn);
        lv_group_add_obj(g_gps_state.layer_modal.group, satellite_btn);
        lv_group_add_obj(g_gps_state.layer_modal.group, contour_btn);
        lv_group_add_obj(g_gps_state.layer_modal.group, close_btn);
        set_default_group(g_gps_state.layer_modal.group);
        bind_navigation_inputs_to_group(g_gps_state.layer_modal.group);
        lv_group_focus_obj(osm_btn);
    }
}

void hide_layer_popup()
{
    if (!g_gps_state.layer_modal.is_open())
    {
        return;
    }

    modal_close(g_gps_state.layer_modal);
    s_layer_source_label = nullptr;
    s_layer_contour_label = nullptr;
    s_layer_source_btns[0] = nullptr;
    s_layer_source_btns[1] = nullptr;
    s_layer_source_btns[2] = nullptr;
    s_layer_contour_btn = nullptr;

    if (app_g != NULL)
    {
        lv_group_set_editing(app_g, false);
        set_default_group(app_g);
        bind_navigation_inputs_to_group(app_g);
        if (g_gps_state.layer_btn != NULL)
        {
            lv_group_focus_obj(g_gps_state.layer_btn);
        }
    }
}

// ============================================================================
// RouteModal Component
// ============================================================================

namespace
{
lv_obj_t* s_route_info_label = nullptr;
lv_obj_t* s_route_hint_label = nullptr;

const char* route_popup_active_path()
{
    if (!g_gps_state.route_file.empty())
    {
        return g_gps_state.route_file.c_str();
    }

    const auto& cfg = app::configFacade().getConfig();
    return cfg.route_path[0] != '\0' ? cfg.route_path : nullptr;
}

const char* route_popup_basename(const char* path)
{
    if (path == nullptr || path[0] == '\0')
    {
        return nullptr;
    }

    const char* slash = std::strrchr(path, '/');
    const char* backslash = std::strrchr(path, '\\');
    const char* base = slash;
    if (backslash != nullptr && (base == nullptr || backslash > base))
    {
        base = backslash;
    }
    return base != nullptr ? (base + 1) : path;
}

void refresh_route_popup_labels()
{
    const char* path = route_popup_active_path();
    const char* base = route_popup_basename(path);

    if (s_route_info_label != nullptr)
    {
        char text[128];
        if (base != nullptr && base[0] != '\0')
        {
            std::snprintf(text, sizeof(text), "Route: %s", base);
        }
        else
        {
            std::snprintf(text, sizeof(text), "Route configured");
        }
        lv_label_set_text(s_route_info_label, text);
    }

    if (s_route_hint_label != nullptr)
    {
        lv_label_set_text(s_route_hint_label,
                          g_gps_state.route_overlay_active ? "Tap Center Route to focus the active route." : "Tap Center Route to load and focus the configured route.");
    }
}

void on_route_focus_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (!gps_route_focus(true))
    {
        show_toast("No route", 1500);
        return;
    }

    hide_route_popup();
}

void on_route_close_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    hide_route_popup();
}

void on_route_button_key(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_KEY)
    {
        return;
    }
    lv_key_t key = static_cast<lv_key_t>(lv_event_get_key(e));
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        hide_route_popup();
    }
}

void on_route_bg_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    lv_obj_t* target = lv_event_get_target_obj(e);
    if (target != g_gps_state.route_modal.bg)
    {
        return;
    }
    hide_route_popup();
}
} // namespace

void show_route_popup()
{
    if (!is_alive())
    {
        return;
    }

    if (!modal_open(g_gps_state.route_modal, lv_screen_active(), app_g))
    {
        return;
    }
    if (g_gps_state.route_modal.bg != nullptr)
    {
        lv_obj_add_event_cb(g_gps_state.route_modal.bg, on_route_bg_clicked, LV_EVENT_CLICKED, nullptr);
    }

    const bool touch_layout = modal_uses_touch_layout();
    modal_set_requested_size(g_gps_state.route_modal, touch_layout ? 500 : 280, touch_layout ? 320 : 164);

    lv_obj_t* win = g_gps_state.route_modal.win;
    if (win == nullptr)
    {
        return;
    }

    lv_obj_t* focus_btn = nullptr;
    lv_obj_t* close_btn = nullptr;

    auto create_route_action_btn = [&](lv_obj_t* parent, const char* text, lv_event_cb_t cb) -> lv_obj_t*
    {
        lv_obj_t* btn = nullptr;
        if (touch_layout)
        {
            btn = modal_create_touch_action_button(parent, text, cb, nullptr, LV_PCT(100));
        }
        else
        {
            btn = lv_btn_create(parent);
            lv_obj_set_size(btn, LV_PCT(100), 22);
            gps::ui::styles::apply_control_button(btn);
            lv_obj_t* label = lv_label_create(btn);
            lv_label_set_text(label, text);
            gps::ui::styles::apply_control_button_label(label);
            lv_obj_center(label);
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
        }
        lv_obj_add_event_cb(btn, on_route_button_key, LV_EVENT_KEY, nullptr);
        return btn;
    };

    if (touch_layout)
    {
        gps::ui::styles::apply_zoom_popup_win(win);
        lv_obj_t* title_bar = modal_create_touch_title_bar(win, "Route");
        const lv_coord_t title_height = title_bar != nullptr ? lv_obj_get_height(title_bar) : modal_resolve_title_height();
        lv_obj_t* content = modal_create_touch_content_area(win, title_height);
        if (content == nullptr)
        {
            return;
        }
        lv_obj_set_style_pad_row(content, 10, 0);

        s_route_info_label = lv_label_create(content);
        gps::ui::styles::apply_control_button_label(s_route_info_label);
        lv_obj_set_width(s_route_info_label, LV_PCT(100));
        lv_label_set_long_mode(s_route_info_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_align(s_route_info_label, LV_TEXT_ALIGN_LEFT, 0);

        s_route_hint_label = lv_label_create(content);
        gps::ui::styles::apply_control_button_label(s_route_hint_label);
        lv_obj_set_width(s_route_hint_label, LV_PCT(100));
        lv_label_set_long_mode(s_route_hint_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(s_route_hint_label, LV_TEXT_ALIGN_LEFT, 0);

        focus_btn = create_route_action_btn(content, "Center Route", on_route_focus_clicked);
        close_btn = create_route_action_btn(content, "Close", on_route_close_clicked);
    }
    else
    {
        lv_obj_t* title = lv_label_create(win);
        lv_label_set_text(title, "Route");
        gps::ui::styles::apply_control_button_label(title);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        s_route_info_label = lv_label_create(win);
        gps::ui::styles::apply_control_button_label(s_route_info_label);
        lv_obj_set_width(s_route_info_label, LV_PCT(100));
        lv_label_set_long_mode(s_route_info_label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(s_route_info_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_route_info_label, LV_ALIGN_TOP_MID, 0, 28);

        s_route_hint_label = lv_label_create(win);
        gps::ui::styles::apply_control_button_label(s_route_hint_label);
        lv_obj_set_width(s_route_hint_label, LV_PCT(100));
        lv_label_set_long_mode(s_route_hint_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(s_route_hint_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_route_hint_label, LV_ALIGN_TOP_MID, 0, 52);

        lv_obj_t* list = lv_obj_create(win);
        lv_obj_set_size(list, LV_PCT(100), 58);
        lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(list, 2, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

        focus_btn = create_route_action_btn(list, "Center Route", on_route_focus_clicked);
        close_btn = create_route_action_btn(list, "Close", on_route_close_clicked);
    }

    refresh_route_popup_labels();

    if (g_gps_state.route_modal.group != NULL)
    {
        lv_group_remove_all_objs(g_gps_state.route_modal.group);
        lv_group_add_obj(g_gps_state.route_modal.group, focus_btn);
        lv_group_add_obj(g_gps_state.route_modal.group, close_btn);
        set_default_group(g_gps_state.route_modal.group);
        bind_navigation_inputs_to_group(g_gps_state.route_modal.group);
        lv_group_focus_obj(focus_btn);
    }
}

void hide_route_popup()
{
    if (!g_gps_state.route_modal.is_open())
    {
        return;
    }

    modal_close(g_gps_state.route_modal);
    s_route_info_label = nullptr;
    s_route_hint_label = nullptr;

    if (app_g != NULL)
    {
        lv_group_set_editing(app_g, false);
        set_default_group(app_g);
        bind_navigation_inputs_to_group(app_g);
        if (g_gps_state.route_btn != NULL)
        {
            lv_group_focus_obj(g_gps_state.route_btn);
        }
    }
}

// ============================================================================
// UI Layout Helper
// ============================================================================

void fix_ui_elements_position()
{
    if (!is_alive() || g_gps_state.map == NULL)
    {
        return;
    }

    if (g_gps_state.panel != NULL)
    {
        lv_obj_align(g_gps_state.panel, LV_ALIGN_TOP_RIGHT, 0, 3);
        lv_obj_move_foreground(g_gps_state.panel);
    }

    if (g_gps_state.member_panel != NULL)
    {
        lv_obj_align(g_gps_state.member_panel, LV_ALIGN_TOP_LEFT, 0, 3);
        lv_obj_move_foreground(g_gps_state.member_panel);
    }

    if (g_gps_state.resolution_label != NULL)
    {
        lv_obj_align(g_gps_state.resolution_label, LV_ALIGN_BOTTOM_LEFT, 10, -10);
        lv_obj_move_foreground(g_gps_state.resolution_label);
    }
    if (g_gps_state.altitude_label != NULL)
    {
        lv_obj_align(g_gps_state.altitude_label, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_move_foreground(g_gps_state.altitude_label);
    }

    if (g_gps_state.pan_h_indicator != NULL)
    {
        lv_obj_align(g_gps_state.pan_h_indicator, LV_ALIGN_BOTTOM_MID, 0, -20);
        lv_obj_move_foreground(g_gps_state.pan_h_indicator);
    }

    if (g_gps_state.pan_v_indicator != NULL)
    {
        lv_obj_align(g_gps_state.pan_v_indicator, LV_ALIGN_LEFT_MID, 20, 0);
        lv_obj_move_foreground(g_gps_state.pan_v_indicator);
    }
}
