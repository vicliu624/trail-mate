#include "ui/menu/menu_runtime.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#endif

#include "app/app_facade_access.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/screen_brightness_steps.h"
#include "platform/ui/time_runtime.h"
#include "platform/ui/walkie_runtime.h"
#if !defined(ARDUINO_T_DECK_PRO)
#include "ui/components/shortcut_help_modal.h"
#endif
#include "ui/formatters.h"
#include "ui/menu/menu_layout.h"
#include "ui/menu/menu_profile.h"
#include "ui/ui_common.h"
#include "ui/ui_status.h"
#include "ui/ui_theme.h"

#if defined(ARDUINO_T_DECK_PRO)
#include "ui/tdeck_pro/text_shell.h"
#endif

namespace ui
{
namespace menu_runtime
{
namespace
{

#if defined(ESP_PLATFORM)
constexpr const char* kTag = "ui-menu-runtime";
#endif
constexpr int kWalkieRecordBarCount = 7;

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
    lv_timer_t* walkie_record_timer = nullptr;
    lv_obj_t* walkie_record_overlay = nullptr;
    lv_obj_t* walkie_record_bars[kWalkieRecordBarCount]{};
#if !defined(ARDUINO_T_DECK_PRO)
    ::ui::components::shortcut_help_modal::State menu_help_modal{};
#endif
    int watch_face_battery = -1;
    bool menu_active = true;
    bool walkie_recording = false;
    uint8_t walkie_record_phase = 0;
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

bool resolveDisplayLocalTime(struct tm* out_tm)
{
    if (!out_tm)
    {
        return false;
    }

    if (::platform::ui::time::localtime_now(out_tm))
    {
        return true;
    }

    const std::time_t now = std::time(nullptr);
    if (now <= 0)
    {
        return false;
    }

    const std::time_t local = ::platform::ui::time::apply_timezone_offset(now);
    const std::tm* tmp = std::gmtime(&local);
    if (!tmp)
    {
        return false;
    }

    *out_tm = *tmp;
    return true;
}

uint32_t selfNodeId()
{
    return app::hasAppFacade() ? app::messagingFacade().getSelfNodeId() : 0;
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
    const uint32_t self_id = selfNodeId();
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

bool isMenuHelpKey(char key)
{
    return key == 'h' || key == 'H';
}

bool isKeyboardBacklightKey(char key)
{
    return key == 'k' || key == 'K';
}

bool isScreenBrightnessKey(char key)
{
    return key == 'b' || key == 'B';
}

bool pagerMenuKeyboardBacklightShortcutEnabled()
{
#if defined(ARDUINO_T_LORA_PAGER)
    return platform::ui::device::supports_keyboard_backlight();
#else
    return false;
#endif
}

bool screenshotShortcutHelpEnabled()
{
#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return true;
#else
    return false;
#endif
}

bool screenBrightnessShortcutEnabled()
{
    return platform::ui::device::supports_screen_brightness() &&
           platform::ui::device::screen_brightness_max() > 0;
}

#if !defined(ARDUINO_T_DECK_PRO)
void closeMenuHelpModal()
{
    ::ui::components::shortcut_help_modal::close(s_runtime.menu_help_modal);
}

void openMenuHelpModal()
{
    if (::ui::components::shortcut_help_modal::is_open(s_runtime.menu_help_modal))
    {
        closeMenuHelpModal();
        return;
    }

    lv_obj_t* parent = s_runtime.screen_root && lv_obj_is_valid(s_runtime.screen_root)
                           ? s_runtime.screen_root
                           : lv_screen_active();
    if (!parent)
    {
        return;
    }

    ::ui::components::shortcut_help_modal::Row rows[7] = {
        {"WASD", nullptr, "Select app"},
        {"Enter", nullptr, "Open app"},
        {"B", nullptr, "Cycle screen brightness"},
        {"K", nullptr, "Cycle keyboard backlight"},
        {"Space", nullptr, "Walkie PTT when monitor is on"},
        {"ALT", "ALT", "Save screenshot"},
        {"H", "Back", "Close help"},
    };
    std::size_t row_count = 2;
    if (screenBrightnessShortcutEnabled())
    {
        rows[row_count++] = {"B", nullptr, "Cycle screen brightness"};
    }
    if (pagerMenuKeyboardBacklightShortcutEnabled())
    {
        rows[row_count++] = {"K", nullptr, "Cycle keyboard backlight"};
    }
    rows[row_count++] = {"Space", nullptr, "Walkie PTT when monitor is on"};
    if (screenshotShortcutHelpEnabled())
    {
        rows[row_count++] = {"ALT", "ALT", "Save screenshot"};
    }
    rows[row_count++] = {"H", "Back", "Close help"};

    ::ui::components::shortcut_help_modal::Config config{};
    config.title = "Main Menu Help";
    config.rows = rows;
    config.row_count = row_count;
    config.width = 304;
    config.height = 176;
    config.restore_group = lv_group_get_default();
    (void)::ui::components::shortcut_help_modal::open(
        s_runtime.menu_help_modal,
        parent,
        config);
}
#endif

uint8_t nextScreenBrightnessLevel()
{
    const uint8_t max_level = platform::ui::device::screen_brightness_max();
    return platform::ui::screen_brightness_steps::nextLevel(
        platform::ui::device::screen_brightness(),
        max_level);
}

bool cycleScreenBrightness()
{
    if (!screenBrightnessShortcutEnabled())
    {
        return false;
    }

    platform::ui::device::set_screen_brightness(nextScreenBrightnessLevel());
    ui::menu_layout::set_bottom_bar_help_text("H Help");
#if !defined(ARDUINO_T_DECK_PRO)
    closeMenuHelpModal();
#endif
    return true;
}

uint8_t nextKeyboardBacklightCycleLevel(uint8_t current, uint8_t max_level)
{
    const uint8_t low = max_level >= 4 ? static_cast<uint8_t>(max_level / 2U) : 1;
    if (current == 0)
    {
        return low;
    }
    if (current < max_level)
    {
        return max_level;
    }
    return 0;
}

bool cycleKeyboardBacklight()
{
    if (!pagerMenuKeyboardBacklightShortcutEnabled())
    {
        return false;
    }

    const uint8_t max_level = platform::ui::device::keyboard_backlight_max();
    if (max_level == 0)
    {
        return false;
    }
    const uint8_t next = nextKeyboardBacklightCycleLevel(
        platform::ui::device::keyboard_backlight(),
        max_level);
    platform::ui::device::set_keyboard_backlight(next);
    ui::menu_layout::set_bottom_bar_help_text("H Help");
#if !defined(ARDUINO_T_DECK_PRO)
    closeMenuHelpModal();
#endif
    return true;
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

    const uint32_t self_id = selfNodeId();
    watchFaceSetNodeId(self_id);
    const int battery = s_runtime.watch_face_battery >= 0 ? s_runtime.watch_face_battery : -1;
    struct tm info
    {
    };
    if (!resolveDisplayLocalTime(&info))
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
#if defined(ARDUINO_T_DECK_PRO)
    {
        char time_str[16] = {};
        ui::tdeck_pro::text_shell::set_time_text(
            formatMenuTime(time_str, sizeof(time_str)) ? time_str : "--:--");
        return;
    }
#endif

    if (s_runtime.time_label == nullptr)
    {
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
        lv_label_set_text(s_runtime.time_label, "--:--");
    }

    updateWatchFaceTime();
}

void refreshBatteryLabel()
{
#if defined(ARDUINO_T_DECK_PRO)
    {
        char battery_str[32] = {};
        const platform::ui::device::BatteryInfo battery = platform::ui::device::battery_info();
        if (battery.level < 0)
        {
            ui::tdeck_pro::text_shell::set_battery_text(battery.charging ? "USB" : "BAT --");
            return;
        }

        platform::ui::device::handle_low_battery(battery);
        ui_format_battery(battery.level, battery.charging, battery_str, sizeof(battery_str));
        ui::tdeck_pro::text_shell::set_battery_text(battery_str);
        refreshBottomBar();
        return;
    }
#endif

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

void updateWalkieRecordBars()
{
    if (s_runtime.walkie_record_overlay == nullptr)
    {
        return;
    }

    const auto status = platform::ui::walkie::get_status();
    uint8_t level = status.tx_level;
    if (level < 8)
    {
        level = static_cast<uint8_t>(18 + ((s_runtime.walkie_record_phase * 7U) % 44U));
    }

    static constexpr uint8_t kBarBias[kWalkieRecordBarCount] = {18, 42, 70, 52, 88, 36, 62};
    for (int i = 0; i < kWalkieRecordBarCount; ++i)
    {
        lv_obj_t* bar = s_runtime.walkie_record_bars[i];
        if (bar == nullptr)
        {
            continue;
        }
        const uint8_t wave =
            static_cast<uint8_t>((level + kBarBias[i] + s_runtime.walkie_record_phase * (i + 3)) % 100);
        lv_coord_t height = static_cast<lv_coord_t>(5 + (wave * 19) / 100);
        if (height > 24)
        {
            height = 24;
        }
        lv_obj_set_height(bar, height);
    }
    s_runtime.walkie_record_phase = static_cast<uint8_t>(s_runtime.walkie_record_phase + 1);
}

void walkieRecordTimerCb(lv_timer_t*)
{
    updateWalkieRecordBars();
}

void ensureWalkieRecordOverlay()
{
    if (s_runtime.walkie_record_overlay != nullptr || s_runtime.menu_panel == nullptr)
    {
        return;
    }

    lv_obj_t* overlay = lv_obj_create(s_runtime.menu_panel);
    s_runtime.walkie_record_overlay = overlay;
    lv_obj_set_size(overlay, 118, 34);
    lv_obj_align(overlay, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0xFFF1D5), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overlay, 1, 0);
    lv_obj_set_style_border_color(overlay, lv_color_hex(0xD28B2E), 0);
    lv_obj_set_style_radius(overlay, 8, 0);
    lv_obj_set_style_shadow_width(overlay, 0, 0);
    lv_obj_set_style_pad_left(overlay, 10, 0);
    lv_obj_set_style_pad_right(overlay, 10, 0);
    lv_obj_set_style_pad_top(overlay, 4, 0);
    lv_obj_set_style_pad_bottom(overlay, 4, 0);
    lv_obj_set_style_pad_column(overlay, 5, 0);
    lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

    for (int i = 0; i < kWalkieRecordBarCount; ++i)
    {
        lv_obj_t* bar = lv_obj_create(overlay);
        s_runtime.walkie_record_bars[i] = bar;
        lv_obj_set_size(bar, 7, 6);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xE55F2A), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 3, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    }
}

void setWalkieRecording(bool recording)
{
#if defined(ARDUINO_T_DECK_PRO)
    s_runtime.walkie_recording = recording;
    ui::tdeck_pro::text_shell::set_walkie_recording(recording);
    return;
#endif

    if (s_runtime.walkie_recording == recording)
    {
        return;
    }

    s_runtime.walkie_recording = recording;
    if (recording)
    {
        ensureWalkieRecordOverlay();
        if (s_runtime.walkie_record_overlay != nullptr)
        {
            lv_obj_clear_flag(s_runtime.walkie_record_overlay, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_runtime.walkie_record_overlay);
        }
        if (s_runtime.walkie_record_timer == nullptr)
        {
            s_runtime.walkie_record_timer = lv_timer_create(walkieRecordTimerCb, 90, nullptr);
            lv_timer_set_repeat_count(s_runtime.walkie_record_timer, -1);
        }
        else
        {
            lv_timer_resume(s_runtime.walkie_record_timer);
        }
        updateWalkieRecordBars();
        return;
    }

    if (s_runtime.walkie_record_overlay != nullptr)
    {
        lv_obj_add_flag(s_runtime.walkie_record_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_runtime.walkie_record_timer != nullptr)
    {
        lv_timer_pause(s_runtime.walkie_record_timer);
    }
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
    lv_obj_t* menu_radio_mod_icon = nullptr;
    lv_obj_t* menu_walkie_monitor_icon = nullptr;
    if (use_menu_status_icons())
    {
        menu_radio_mod_icon = lv_image_create(menu_status_row);
        menu_walkie_monitor_icon = lv_image_create(menu_status_row);
        menu_route_icon = lv_image_create(menu_status_row);
        menu_tracker_icon = lv_image_create(menu_status_row);
        menu_gps_icon = lv_image_create(menu_status_row);
        menu_wifi_icon = lv_image_create(menu_status_row);
        menu_team_icon = lv_image_create(menu_status_row);
        menu_msg_icon = lv_image_create(menu_status_row);
#if defined(TRAIL_MATE_ENABLE_BLE) && TRAIL_MATE_ENABLE_BLE
        menu_ble_icon = lv_image_create(menu_status_row);
#endif
        lv_obj_add_flag(menu_radio_mod_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_walkie_monitor_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_route_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_tracker_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_gps_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_wifi_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_team_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_msg_icon, LV_OBJ_FLAG_HIDDEN);
#if defined(TRAIL_MATE_ENABLE_BLE) && TRAIL_MATE_ENABLE_BLE
        lv_obj_add_flag(menu_ble_icon, LV_OBJ_FLAG_HIDDEN);
#endif
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
        menu_ble_icon,
        menu_radio_mod_icon,
        menu_walkie_monitor_icon);
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
#if defined(ARDUINO_T_DECK_PRO)
    // The text shell refreshes status at explicit lifecycle boundaries and
    // user actions. A clock/battery timer would otherwise issue a periodic
    // EPD update while the menu is idle.
    return;
#else
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
#endif
}

} // namespace

void init(lv_obj_t* screen_root, lv_obj_t* main_screen, lv_obj_t* menu_panel, const Hooks& hooks)
{
    s_runtime.hooks = hooks;
    s_runtime.screen_root = screen_root;
    s_runtime.main_screen = main_screen;
    s_runtime.menu_panel = menu_panel;

#if !defined(ARDUINO_T_DECK_PRO)
    createTopBar();
#endif
    ui::menu_layout::bringContentToFront();
#if !defined(ARDUINO_T_DECK_PRO)
    ui::status::init();
#endif
#if !defined(ARDUINO_T_DECK_PRO)
    initWatchFace();
#endif
    createTimers();
    refreshTimeLabel();
    refreshBatteryLabel();
    refreshBottomBar();
    setScene(Scene::Menu);
}

void showWatchFace()
{
#if defined(ARDUINO_T_DECK_PRO)
    // A sleeping Pro returns to the text menu.  The old decorative watch face
    // is intentionally not instantiated on this profile.
    showMainMenu();
    return;
#endif

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

void setMenuActive(bool active)
{
    s_runtime.menu_active = active;
    if (!active)
    {
        platform::ui::walkie::set_ptt(false);
        setWalkieRecording(false);
#if !defined(ARDUINO_T_DECK_PRO)
        closeMenuHelpModal();
#endif
    }

#if !defined(ARDUINO_T_DECK_PRO)
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
#endif

    if (active)
    {
        refreshTimeLabel();
        refreshBatteryLabel();
        refreshBottomBar();
    }
}

bool handleWalkieKey(char key, int state)
{
    if (key != ' ' || currentScene() != Scene::Menu)
    {
        return false;
    }

    const auto status = platform::ui::walkie::get_status();
    if (!status.monitor_enabled || !status.active)
    {
        if (state == 0)
        {
            setWalkieRecording(false);
        }
        return false;
    }

    const bool pressed = state != 0;
    platform::ui::walkie::set_ptt(pressed);
    setWalkieRecording(pressed);
    return true;
}

bool handleShortcutKey(char key, int state)
{
    if (state == 0 || currentScene() != Scene::Menu)
    {
        return false;
    }

    if (isMenuHelpKey(key))
    {
#if defined(ARDUINO_T_DECK_PRO)
        // The retired modal is not part of the fixed text-shell grammar.
        // Keep the key consumed so it cannot create a legacy overlay.
        return true;
#else
        openMenuHelpModal();
        return true;
#endif
    }

    if (isScreenBrightnessKey(key))
    {
        return cycleScreenBrightness();
    }

    if (isKeyboardBacklightKey(key))
    {
        return cycleKeyboardBacklight();
    }

    return false;
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
