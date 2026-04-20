#include "ui/menu/menu_runtime.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#endif

#include "app/app_facade_access.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/time_runtime.h"
#include "ui/menu/menu_layout.h"
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

#if defined(ESP_PLATFORM)
constexpr const char* kTag = "ui-menu-runtime";
#endif

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
    bool menu_active = true;
    Scene scene = Scene::Menu;
};

RuntimeState s_runtime;

bool use_menu_status_icons()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return false;
#else
    return true;
#endif
}

bool formatMenuTime(char* out, size_t out_len)
{
    return s_runtime.hooks.format_time ? s_runtime.hooks.format_time(out, out_len) : false;
}

std::size_t used_bytes(std::size_t total_bytes, std::size_t free_bytes)
{
    return total_bytes > free_bytes ? (total_bytes - free_bytes) : 0;
}

void format_memory_value(std::size_t bytes, char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    if (bytes >= (1024U * 1024U))
    {
        const unsigned whole = static_cast<unsigned>(bytes / (1024U * 1024U));
        const unsigned tenth =
            static_cast<unsigned>((bytes % (1024U * 1024U)) * 10U / (1024U * 1024U));
        if (tenth == 0U)
        {
            std::snprintf(out, out_len, "%uM", whole);
        }
        else
        {
            std::snprintf(out, out_len, "%u.%uM", whole, tenth);
        }
        return;
    }

    std::snprintf(out,
                  out_len,
                  "%luK",
                  static_cast<unsigned long>((bytes + 1023U) / 1024U));
}

void refreshBottomBar()
{
    char node_text[24];
    const uint32_t self_id = app::messagingFacade().getSelfNodeId();
    if (self_id != 0)
    {
        std::snprintf(node_text, sizeof(node_text), "!%08lX", static_cast<unsigned long>(self_id));
    }
    else
    {
        std::snprintf(node_text, sizeof(node_text), "-");
    }
    ui::menu_layout::set_bottom_bar_node_text(node_text);

    const platform::ui::device::MemoryStats stats = platform::ui::device::memory_stats();
    char ram_used[16];
    char ram_total[16];
    char psram_used[16];
    char psram_total[16];
    char ram_text[32];
    char psram_text[32];

    format_memory_value(used_bytes(stats.ram_total_bytes, stats.ram_free_bytes), ram_used, sizeof(ram_used));
    format_memory_value(stats.ram_total_bytes, ram_total, sizeof(ram_total));
    std::snprintf(ram_text, sizeof(ram_text), "%s/%s", ram_used, ram_total);
    ui::menu_layout::set_bottom_bar_ram_text(ram_text);

    if (stats.psram_available && stats.psram_total_bytes > 0)
    {
        format_memory_value(used_bytes(stats.psram_total_bytes, stats.psram_free_bytes),
                            psram_used,
                            sizeof(psram_used));
        format_memory_value(stats.psram_total_bytes, psram_total, sizeof(psram_total));
        std::snprintf(psram_text, sizeof(psram_text), "%s/%s", psram_used, psram_total);
        ui::menu_layout::set_bottom_bar_psram_text(psram_text);
        ui::menu_layout::set_bottom_bar_psram_visible(true);
    }
    else
    {
        ui::menu_layout::set_bottom_bar_psram_visible(false);
    }
}

void showMainMenu()
{
#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag, "showMainMenu hook");
#endif
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

    struct tm info
    {
    };
    if (!::platform::ui::time::localtime_now(&info))
    {
        watchFaceSetTime(-1, -1, -1, -1, nullptr, battery);
        return;
    }

    char weekday[8] = "---";
    strftime(weekday, sizeof(weekday), "%a", &info);
    watchFaceSetTime(info.tm_hour, info.tm_min, info.tm_mon + 1, info.tm_mday, weekday, battery);
}

void hideWatchFaceInternal()
{
    if (!watchFaceReady() || s_runtime.main_screen == nullptr)
    {
        return;
    }
#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag, "hideWatchFaceInternal");
#endif
    watchFaceShow(false);
    lv_obj_clear_flag(s_runtime.main_screen, LV_OBJ_FLAG_HIDDEN);
}

void watchFaceUnlock()
{
#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag, "watchFaceUnlock");
#endif
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
    refreshBottomBar();
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
    lv_obj_move_background(menu_topbar);

    s_runtime.time_label = lv_label_create(s_runtime.menu_panel);
    lv_obj_set_width(s_runtime.time_label, LV_SIZE_CONTENT);
    lv_obj_align(s_runtime.time_label, LV_ALIGN_TOP_LEFT, profile.top_bar_side_inset, 0);
    lv_obj_set_style_text_align(s_runtime.time_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(s_runtime.time_label, ui::theme::text(), 0);
    lv_obj_set_style_bg_opa(s_runtime.time_label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_runtime.time_label, profile.top_bar_text_pad, 0);
    lv_obj_set_style_text_font(s_runtime.time_label, profile.top_bar_font, 0);
    lv_label_set_text(s_runtime.time_label, "--:--");
    lv_obj_move_foreground(s_runtime.time_label);

    s_runtime.battery_label = lv_label_create(s_runtime.menu_panel);
    lv_obj_set_width(s_runtime.battery_label, LV_SIZE_CONTENT);
    lv_obj_align(s_runtime.battery_label, LV_ALIGN_TOP_RIGHT, -profile.top_bar_side_inset, 0);
    lv_obj_set_style_text_align(s_runtime.battery_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_runtime.battery_label, ui::theme::text(), 0);
    lv_obj_set_style_bg_opa(s_runtime.battery_label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_runtime.battery_label, profile.top_bar_text_pad, 0);
    lv_obj_set_style_text_font(s_runtime.battery_label, profile.top_bar_font, 0);
    lv_label_set_text(s_runtime.battery_label, "--");
    lv_obj_move_foreground(s_runtime.battery_label);

    lv_obj_t* menu_status_row = lv_obj_create(s_runtime.menu_panel);
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
    lv_obj_move_foreground(menu_status_row);

    lv_obj_t* menu_route_icon = nullptr;
    lv_obj_t* menu_tracker_icon = nullptr;
    lv_obj_t* menu_gps_icon = nullptr;
    lv_obj_t* menu_wifi_icon = nullptr;
    lv_obj_t* menu_team_icon = nullptr;
    lv_obj_t* menu_msg_icon = nullptr;
    lv_obj_t* menu_ble_icon = nullptr;
    if (use_menu_status_icons())
    {
        menu_route_icon = lv_image_create(menu_status_row);
        menu_tracker_icon = lv_image_create(menu_status_row);
        menu_gps_icon = lv_image_create(menu_status_row);
        menu_wifi_icon = lv_image_create(menu_status_row);
        menu_team_icon = lv_image_create(menu_status_row);
        menu_msg_icon = lv_image_create(menu_status_row);
        menu_ble_icon = lv_image_create(menu_status_row);
        lv_obj_add_flag(menu_route_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_tracker_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_gps_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_wifi_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_team_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_msg_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_ble_icon, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(menu_status_row, LV_OBJ_FLAG_HIDDEN);
    }

    ui::status::register_menu_status_row(
        menu_status_row,
        menu_route_icon,
        menu_tracker_icon,
        menu_gps_icon,
        menu_wifi_icon,
        menu_team_icon,
        menu_msg_icon,
        menu_ble_icon);
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
    ui::menu_layout::bringContentToFront();
    ui::status::init();
    initWatchFace();
    createTimers();
    refreshTimeLabel();
    refreshBatteryLabel();
    refreshBottomBar();
    setScene(Scene::Menu);
}

void showWatchFace()
{
    if (!watchFaceReady() || s_runtime.main_screen == nullptr)
    {
        return;
    }
#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag, "showWatchFace");
#endif
    showMainMenu();
    lv_obj_clear_flag(s_runtime.main_screen, LV_OBJ_FLAG_HIDDEN);
    watchFaceShow(true);
    setScene(Scene::WatchFace);
    updateWatchFaceTime();
}

void onWakeFromSleep()
{
#if defined(ESP_PLATFORM)
    ESP_LOGI(kTag, "onWakeFromSleep");
#endif
    showWatchFace();
}

void setMenuActive(bool active)
{
    s_runtime.menu_active = active;

    if (s_runtime.time_timer != nullptr)
    {
        if (active)
        {
            lv_timer_resume(s_runtime.time_timer);
        }
        else
        {
            lv_timer_pause(s_runtime.time_timer);
        }
    }

    if (s_runtime.battery_timer != nullptr)
    {
        if (active)
        {
            lv_timer_resume(s_runtime.battery_timer);
        }
        else
        {
            lv_timer_pause(s_runtime.battery_timer);
        }
    }

    if (active)
    {
        refreshTimeLabel();
        refreshBatteryLabel();
        refreshBottomBar();
    }
}

void setScene(Scene scene)
{
    s_runtime.scene = scene;
    switch (scene)
    {
    case Scene::Menu:
        setMenuActive(true);
        break;
    case Scene::App:
    case Scene::WatchFace:
    case Scene::Sleeping:
        setMenuActive(false);
        break;
    }
}

Scene currentScene()
{
    return s_runtime.scene;
}

} // namespace menu_runtime
} // namespace ui
