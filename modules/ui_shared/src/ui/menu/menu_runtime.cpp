#include "ui/menu/menu_runtime.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "app/app_facade_access.h"
#include "platform/ui/device_runtime.h"
#include "ui/menu/menu_profile.h"
#include "ui/ui_common.h"
#include "ui/ui_status.h"
#include "ui/ui_theme.h"

namespace ui
{
namespace menu_runtime
{
namespace
{

struct RuntimeState
{
    Hooks hooks{};
    lv_obj_t* screen_root = nullptr;
    lv_obj_t* main_screen = nullptr;
    lv_obj_t* menu_panel = nullptr;
    lv_obj_t* time_label = nullptr;
    lv_obj_t* battery_label = nullptr;
    lv_timer_t* time_timer = nullptr;
    lv_timer_t* battery_timer = nullptr;
    int watch_face_battery = -1;
};

RuntimeState s_runtime;

bool formatMenuTime(char* out, size_t out_len)
{
    return s_runtime.hooks.format_time ? s_runtime.hooks.format_time(out, out_len) : false;
}

void showMainMenu()
{
    if (s_runtime.hooks.show_main_menu)
    {
        s_runtime.hooks.show_main_menu();
    }
}

bool watchFaceReady()
{
    return s_runtime.hooks.watch_face.is_ready ? s_runtime.hooks.watch_face.is_ready() : false;
}

void watchFaceSetNodeId(uint32_t node_id)
{
    if (s_runtime.hooks.watch_face.set_node_id)
    {
        s_runtime.hooks.watch_face.set_node_id(node_id);
    }
}

void watchFaceSetTime(int hour, int minute, int month, int day, const char* weekday, int battery_percent)
{
    if (s_runtime.hooks.watch_face.set_time)
    {
        s_runtime.hooks.watch_face.set_time(hour, minute, month, day, weekday, battery_percent);
    }
}

void watchFaceShow(bool show)
{
    if (s_runtime.hooks.watch_face.show)
    {
        s_runtime.hooks.watch_face.show(show);
    }
}

void updateWatchFaceTime()
{
    if (!watchFaceReady())
    {
        return;
    }

    const uint32_t self_id = app::messagingFacade().getSelfNodeId();
    watchFaceSetNodeId(self_id);
    const int battery = s_runtime.watch_face_battery >= 0 ? s_runtime.watch_face_battery : -1;
    if (!platform::ui::device::rtc_ready())
    {
        watchFaceSetTime(-1, -1, -1, -1, nullptr, battery);
        return;
    }

    const time_t now = time(nullptr);
    if (now <= 0)
    {
        watchFaceSetTime(-1, -1, -1, -1, nullptr, battery);
        return;
    }

    const time_t local = ui_apply_timezone_offset(now);
    struct tm* info = gmtime(&local);
    if (!info)
    {
        watchFaceSetTime(-1, -1, -1, -1, nullptr, battery);
        return;
    }

    char weekday[8] = "---";
    strftime(weekday, sizeof(weekday), "%a", info);
    watchFaceSetTime(info->tm_hour, info->tm_min, info->tm_mon + 1, info->tm_mday, weekday, battery);
}

void hideWatchFaceInternal()
{
    if (!watchFaceReady() || s_runtime.main_screen == nullptr)
    {
        return;
    }
    watchFaceShow(false);
    lv_obj_clear_flag(s_runtime.main_screen, LV_OBJ_FLAG_HIDDEN);
}

void watchFaceUnlock()
{
    hideWatchFaceInternal();
    showMainMenu();
}

void refreshTimeLabel()
{
    if (s_runtime.time_label == nullptr)
    {
        updateWatchFaceTime();
        return;
    }

    if (!platform::ui::device::rtc_ready())
    {
        lv_label_set_text(s_runtime.time_label, "--:--");
        updateWatchFaceTime();
        return;
    }

    char time_str[16];
    if (formatMenuTime(time_str, sizeof(time_str)))
    {
        static char last_time_str[16] = "";
        if (strcmp(time_str, last_time_str) != 0)
        {
            lv_label_set_text(s_runtime.time_label, time_str);
            strncpy(last_time_str, time_str, sizeof(last_time_str) - 1);
            last_time_str[sizeof(last_time_str) - 1] = '\0';
        }
    }
    else
    {
        lv_label_set_text(s_runtime.time_label, "??:??");
    }

    updateWatchFaceTime();
}

void refreshBatteryLabel()
{
    if (s_runtime.battery_label == nullptr)
    {
        return;
    }

    char battery_str[32];
    const platform::ui::device::BatteryInfo battery = platform::ui::device::battery_info();
    const bool charging = battery.charging;
    const int level = battery.level;
    if (level < 0)
    {
        s_runtime.watch_face_battery = -1;
        lv_label_set_text(s_runtime.battery_label, charging ? "USB" : "--");
        updateWatchFaceTime();
        return;
    }

    platform::ui::device::handle_low_battery(battery);

    s_runtime.watch_face_battery = level;

    ui_format_battery(level, charging, battery_str, sizeof(battery_str));

    static char last_battery_str[32] = "";
    if (strcmp(battery_str, last_battery_str) != 0)
    {
        lv_label_set_text(s_runtime.battery_label, battery_str);
        strncpy(last_battery_str, battery_str, sizeof(last_battery_str) - 1);
        last_battery_str[sizeof(last_battery_str) - 1] = '\0';
    }

    updateWatchFaceTime();
}

void createTopBar()
{
    const auto& profile = ui::menu_profile::current();
    lv_obj_t* menu_topbar = lv_obj_create(s_runtime.menu_panel);
    lv_obj_set_size(menu_topbar, LV_PCT(100), profile.top_bar_height);
    lv_obj_align(menu_topbar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(menu_topbar, ui::theme::accent(), 0);
    lv_obj_set_style_bg_opa(menu_topbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(menu_topbar, 0, 0);
    lv_obj_set_style_radius(menu_topbar, 0, 0);
    lv_obj_set_style_shadow_width(menu_topbar, 0, 0);
    lv_obj_set_style_pad_all(menu_topbar, 0, 0);
    lv_obj_clear_flag(menu_topbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(menu_topbar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(menu_topbar);

    s_runtime.time_label = lv_label_create(menu_topbar);
    lv_obj_set_width(s_runtime.time_label, LV_SIZE_CONTENT);
    lv_obj_align(s_runtime.time_label, LV_ALIGN_TOP_LEFT, profile.top_bar_side_inset, 0);
    lv_obj_set_style_text_align(s_runtime.time_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(s_runtime.time_label, ui::theme::text(), 0);
    lv_obj_set_style_bg_opa(s_runtime.time_label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_runtime.time_label, profile.top_bar_text_pad, 0);
    lv_obj_set_style_text_font(s_runtime.time_label, profile.top_bar_font, 0);
    lv_label_set_text(s_runtime.time_label, "--:--");

    s_runtime.battery_label = lv_label_create(menu_topbar);
    lv_obj_set_width(s_runtime.battery_label, LV_SIZE_CONTENT);
    lv_obj_align(s_runtime.battery_label, LV_ALIGN_TOP_RIGHT, -profile.top_bar_side_inset, 0);
    lv_obj_set_style_text_align(s_runtime.battery_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_runtime.battery_label, ui::theme::text(), 0);
    lv_obj_set_style_bg_opa(s_runtime.battery_label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_runtime.battery_label, profile.top_bar_text_pad, 0);
    lv_obj_set_style_text_font(s_runtime.battery_label, profile.top_bar_font, 0);
    lv_label_set_text(s_runtime.battery_label, "--");

    lv_obj_t* menu_status_row = lv_obj_create(menu_topbar);
    lv_obj_set_size(menu_status_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(menu_status_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(menu_status_row, 0, 0);
    lv_obj_set_style_pad_all(menu_status_row, 0, 0);
    lv_obj_set_style_pad_column(menu_status_row, profile.status_row_gap, 0);
    lv_obj_set_style_radius(menu_status_row, 0, 0);
    lv_obj_clear_flag(menu_status_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(menu_status_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(menu_status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(menu_status_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(menu_status_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(menu_status_row, LV_ALIGN_TOP_MID, 0, profile.status_row_offset_y);

    lv_obj_t* menu_route_icon = lv_image_create(menu_status_row);
    lv_obj_t* menu_tracker_icon = lv_image_create(menu_status_row);
    lv_obj_t* menu_gps_icon = lv_image_create(menu_status_row);
    lv_obj_t* menu_team_icon = lv_image_create(menu_status_row);
    lv_obj_t* menu_msg_icon = lv_image_create(menu_status_row);
    lv_obj_t* menu_ble_icon = lv_image_create(menu_status_row);
    lv_obj_add_flag(menu_route_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(menu_tracker_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(menu_gps_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(menu_team_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(menu_msg_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(menu_ble_icon, LV_OBJ_FLAG_HIDDEN);

    ui::status::register_menu_status_row(
        menu_status_row, menu_route_icon, menu_tracker_icon, menu_gps_icon, menu_team_icon, menu_msg_icon, menu_ble_icon);
}

void initWatchFace()
{
    if (s_runtime.hooks.watch_face.create)
    {
        s_runtime.hooks.watch_face.create(s_runtime.screen_root);
    }
    if (s_runtime.hooks.watch_face.set_unlock_cb)
    {
        s_runtime.hooks.watch_face.set_unlock_cb(watchFaceUnlock);
    }
    watchFaceShow(false);
}

void createTimers()
{
    constexpr uint32_t time_update_interval_ms = 60000;
    constexpr uint32_t battery_update_interval_ms = 60000;

    s_runtime.time_timer = lv_timer_create(
        [](lv_timer_t* timer)
        {
            (void)timer;
            refreshTimeLabel();
        },
        time_update_interval_ms,
        nullptr);
    lv_timer_set_repeat_count(s_runtime.time_timer, -1);

    s_runtime.battery_timer = lv_timer_create(
        [](lv_timer_t* timer)
        {
            (void)timer;
            refreshBatteryLabel();
        },
        battery_update_interval_ms,
        nullptr);
    lv_timer_set_repeat_count(s_runtime.battery_timer, -1);
}

} // namespace

void init(lv_obj_t* screen_root, lv_obj_t* main_screen, lv_obj_t* menu_panel, const Hooks& hooks)
{
    s_runtime.hooks = hooks;
    s_runtime.screen_root = screen_root;
    s_runtime.main_screen = main_screen;
    s_runtime.menu_panel = menu_panel;

    createTopBar();
    ui::status::init();
    initWatchFace();
    createTimers();
    refreshTimeLabel();
    refreshBatteryLabel();
}

void showWatchFace()
{
    if (!watchFaceReady() || s_runtime.main_screen == nullptr)
    {
        return;
    }
    showMainMenu();
    lv_obj_clear_flag(s_runtime.main_screen, LV_OBJ_FLAG_HIDDEN);
    watchFaceShow(true);
    updateWatchFaceTime();
}

void onWakeFromSleep()
{
    showWatchFace();
}

} // namespace menu_runtime
} // namespace ui
