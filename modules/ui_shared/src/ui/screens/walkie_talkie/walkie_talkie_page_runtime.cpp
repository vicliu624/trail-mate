#include "ui/screens/walkie_talkie/walkie_talkie_page_runtime.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM) || defined(TRAIL_MATE_PLATFORM_LINUX)

#include "platform/ui/device_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/walkie_runtime.h"
#include "ui/app_runtime.h"
#include "ui/input/pager_navigation.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/ui_common.h"
#include "ui/ui_status.h"
#include "ui/widgets/top_bar.h"

#include <algorithm>
#include <cstdio>

#if !defined(LV_FONT_MONTSERRAT_18) || !LV_FONT_MONTSERRAT_18
#define lv_font_montserrat_18 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_28) || !LV_FONT_MONTSERRAT_28
#define lv_font_montserrat_28 lv_font_montserrat_18
#endif

using Host = walkie_page::ui::shell::Host;

namespace
{

const Host* s_host = nullptr;
lv_obj_t* s_root = nullptr;
lv_obj_t* s_freq_label = nullptr;
lv_obj_t* s_mod_label = nullptr;
lv_obj_t* s_mode_label = nullptr;
lv_obj_t* s_left_fill = nullptr;
lv_obj_t* s_right_fill = nullptr;
lv_obj_t* s_monitor_row = nullptr;
lv_obj_t* s_monitor_switch = nullptr;
lv_obj_t* s_monitor_label = nullptr;
lv_obj_t* s_control_panel = nullptr;
lv_obj_t* s_ptt_button = nullptr;
lv_obj_t* s_volume_bar = nullptr;
lv_obj_t* s_volume_label = nullptr;
lv_timer_t* s_timer = nullptr;
ui::widgets::TopBar s_top_bar;
bool s_started = false;

constexpr uint32_t kAmber = 0xEBA341;
constexpr uint32_t kAmberDark = 0xC98118;
constexpr uint32_t kWarmBg = 0xF6E6C6;
constexpr uint32_t kPanelBg = 0xFAF0D8;
constexpr uint32_t kLine = 0xE7C98F;
constexpr uint32_t kText = 0x6B4A1E;
constexpr uint32_t kTextDim = 0x8A6A3A;
constexpr uint32_t kOk = 0x3E7D3E;
constexpr uint32_t kWarn = 0xB94A2C;
constexpr uint32_t kGray = 0x6E6E6E;

constexpr lv_coord_t kControlPanelMaxWidth = 320;
constexpr lv_coord_t kControlPanelMinWidth = 238;
constexpr lv_coord_t kControlPanelSideMargin = 72;
constexpr lv_coord_t kVolumeBarHeight = 8;
constexpr lv_coord_t kLevelMeterHeight = 8;
constexpr lv_coord_t kMonitorRowHeight = 34;
constexpr lv_coord_t kMonitorSwitchWidth = 46;
constexpr lv_coord_t kMonitorSwitchHeight = 24;

void set_monitor_visual_state(bool enabled)
{
    if (s_monitor_row)
    {
        if (enabled)
        {
            lv_obj_add_state(s_monitor_row, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_clear_state(s_monitor_row, LV_STATE_CHECKED);
        }
    }
    if (s_monitor_switch)
    {
        if (enabled)
        {
            lv_obj_add_state(s_monitor_switch, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_clear_state(s_monitor_switch, LV_STATE_CHECKED);
        }
    }
    if (s_monitor_label)
    {
        ::ui::i18n::set_label_text(s_monitor_label, enabled ? "Monitor On" : "Monitor Off");
        lv_obj_set_style_text_color(s_monitor_label,
                                    lv_color_hex(enabled ? kText : kTextDim),
                                    LV_PART_MAIN);
    }
}

void apply_monitor_enabled_from_ui(bool enabled)
{
    std::printf("[WALKIE][UI] monitor request=%d current=%d\n",
                enabled ? 1 : 0,
                platform::ui::walkie::monitor_enabled() ? 1 : 0);
    if (!platform::ui::walkie::set_monitor_enabled(enabled))
    {
        const bool actual = platform::ui::walkie::get_status().monitor_enabled;
        set_monitor_visual_state(actual);
        const char* detail = platform::ui::walkie::last_error();
        ::ui::feedback::show_notice((detail && detail[0] != '\0') ? detail : "Monitor unavailable",
                                    2500);
        return;
    }

    set_monitor_visual_state(platform::ui::walkie::get_status().monitor_enabled);
    ::ui::status::force_update();
}

void toggle_monitor_from_ui()
{
    apply_monitor_enabled_from_ui(!platform::ui::walkie::monitor_enabled());
}

void request_exit()
{
    if (s_host)
    {
        ::ui::page::request_exit(s_host);
        return;
    }
    ui_request_exit_to_menu();
}

void on_back(void*)
{
    request_exit();
}

void root_key_event_cb(lv_event_t* e)
{
    uint32_t key = lv_event_get_key(e);
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
    if (key == 't' || key == 'T')
    {
        const auto status = platform::ui::walkie::get_status();
        if (status.active && !ui_is_overlay_active() && !::ui::input::pager::help_open())
            platform::ui::walkie::set_ptt(!status.tx);
        lv_event_stop_bubbling(e);
        lv_event_stop_processing(e);
        return;
    }
#endif
    if (key != LV_KEY_BACKSPACE && key != LV_KEY_ESC)
    {
        return;
    }
    on_back(nullptr);
}

void monitor_row_event_cb(lv_event_t* e)
{
    if (!s_monitor_row)
    {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED)
    {
        toggle_monitor_from_ui();
        lv_event_stop_processing(e);
        return;
    }
    if (code != LV_EVENT_KEY)
    {
        return;
    }

    const uint32_t key = lv_event_get_key(e);
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
    if (key == 't' || key == 'T')
    {
        root_key_event_cb(e);
        return;
    }
#endif
    if (key == LV_KEY_ENTER || key == ' ')
    {
        toggle_monitor_from_ui();
        lv_event_stop_processing(e);
        return;
    }
    if (key == LV_KEY_BACKSPACE || key == LV_KEY_ESC)
    {
        on_back(nullptr);
        lv_event_stop_processing(e);
    }
}

void style_monitor_row(lv_obj_t* row)
{
    if (!row)
    {
        return;
    }
    lv_obj_set_style_bg_color(row, lv_color_hex(kPanelBg), LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_hex(kAmber), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_radius(row, 8, LV_PART_MAIN);
    lv_obj_set_style_outline_width(row, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(row, lv_color_hex(kAmberDark), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(row, 2, LV_STATE_FOCUSED);
}

void style_monitor_switch(lv_obj_t* sw)
{
    if (!sw)
    {
        return;
    }
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(sw, lv_color_hex(kWarmBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(sw, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(kGray), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sw, LV_OPA_30, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sw, lv_color_hex(kAmber), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(kPanelBg), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_outline_width(sw, 0, LV_STATE_FOCUSED);
}

void style_control_panel(lv_obj_t* panel)
{
    if (!panel)
    {
        return;
    }
    lv_obj_set_style_bg_color(panel, lv_color_hex(kPanelBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_left(panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);
}

void style_readonly_volume_bar(lv_obj_t* bar)
{
    if (!bar)
    {
        return;
    }
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kWarmBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kOk), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_outline_width(bar, 0, LV_STATE_FOCUSED);
}

void update_vu(lv_obj_t* fill, uint8_t level)
{
    if (!fill)
    {
        return;
    }
    lv_obj_t* parent = lv_obj_get_parent(fill);
    if (!parent)
    {
        return;
    }
    const lv_coord_t width = lv_obj_get_width(parent);
    const lv_coord_t height = lv_obj_get_height(parent);
    if (width > height * 2)
    {
        lv_coord_t fill_w = static_cast<lv_coord_t>((level * width) / 100);
        if (fill_w < 0)
        {
            fill_w = 0;
        }
        if (fill_w > width)
        {
            fill_w = width;
        }
        lv_obj_set_size(fill, fill_w, LV_PCT(100));
        lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);
        return;
    }
    lv_coord_t fill_h = static_cast<lv_coord_t>((level * height) / 100);
    if (fill_h < 0)
    {
        fill_h = 0;
    }
    if (fill_h > height)
    {
        fill_h = height;
    }
    lv_obj_set_size(fill, LV_PCT(100), fill_h);
    lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void set_freq_text(float freq_mhz);
void set_error_text(const char* message);

void ptt_event_cb(lv_event_t* event)
{
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) platform::ui::walkie::set_ptt(true);
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST ||
             code == LV_EVENT_DEFOCUSED || code == LV_EVENT_DELETE)
        platform::ui::walkie::set_ptt(false);
}

void set_error_text(const char* message);

void refresh_cb(lv_timer_t*)
{
    platform::ui::walkie::Status st = platform::ui::walkie::get_status();
    ui_update_top_bar_battery(s_top_bar);
    if (!st.active)
    {
        const char* error = platform::ui::walkie::last_error();
        set_error_text(error && *error ? error : "Starting...");
        return;
    }
    if (s_monitor_row) lv_obj_clear_state(s_monitor_row, LV_STATE_DISABLED);
    if (s_monitor_switch) lv_obj_clear_state(s_monitor_switch, LV_STATE_DISABLED);
    if (s_ptt_button) lv_obj_clear_state(s_ptt_button, LV_STATE_DISABLED);
    if (s_mod_label) ::ui::i18n::set_label_text(s_mod_label, "FSK");
    if (s_freq_label) lv_obj_clear_flag(s_freq_label, LV_OBJ_FLAG_HIDDEN);
    set_freq_text(st.freq_mhz);
    if (s_mode_label)
    {
        ::ui::i18n::set_label_text(s_mode_label, st.tx ? "TALK" : "LISTEN");
        lv_obj_set_style_text_color(s_mode_label,
                                    lv_color_hex(st.tx ? kWarn : kOk),
                                    LV_PART_MAIN);
    }
    if (s_monitor_switch)
    {
        set_monitor_visual_state(st.monitor_enabled);
    }
    update_vu(s_left_fill, st.tx ? st.tx_level : st.rx_level);
    update_vu(s_right_fill, st.tx ? st.tx_level : st.rx_level);

    if (s_volume_bar)
    {
        int vol = platform::ui::walkie::volume();
        lv_bar_set_value(s_volume_bar, vol, LV_ANIM_OFF);
    }
    if (s_volume_label)
    {
        char buf[24];
        int vol = platform::ui::walkie::volume();
        snprintf(buf, sizeof(buf), "%s", ::ui::i18n::format("VOL %d", vol).c_str());
        ::ui::i18n::set_label_text_raw(s_volume_label, buf);
    }
}

void set_freq_text(float freq_mhz)
{
    if (!s_freq_label)
    {
        return;
    }
    char buf[24];
    if (freq_mhz <= 0.0f)
    {
        snprintf(buf, sizeof(buf), "--.- MHz");
    }
    else
    {
        snprintf(buf, sizeof(buf), "%.3f MHz", static_cast<double>(freq_mhz));
    }
    ::ui::i18n::set_label_text_raw(s_freq_label, buf);
}

void set_error_text(const char* message)
{
    if (s_freq_label)
    {
        ::ui::i18n::set_label_text(s_freq_label, "Walkie Talkie");
        if (lv_display_get_vertical_resolution(nullptr) <= 200)
            lv_obj_add_flag(s_freq_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_mod_label)
    {
        ::ui::i18n::set_label_text(s_mod_label, "ERR");
    }
    if (s_mode_label)
    {
        auto* row = lv_obj_get_parent(s_mode_label);
        lv_obj_update_layout(row);
        lv_obj_set_width(s_mode_label, std::max<lv_coord_t>(100, lv_obj_get_width(row) - 70));
        lv_label_set_long_mode(s_mode_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(s_mode_label, ::ui::page_profile::resolve_body_font(), 0);
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        ::ui::i18n::set_label_text(s_mode_label,
                                   message ? message : "Walkie not available");
        lv_obj_set_style_text_color(s_mode_label, lv_color_hex(kWarn), LV_PART_MAIN);
    }
    update_vu(s_left_fill, 0);
    update_vu(s_right_fill, 0);
    if (s_monitor_row) lv_obj_add_state(s_monitor_row, LV_STATE_DISABLED);
    if (s_monitor_switch) lv_obj_add_state(s_monitor_switch, LV_STATE_DISABLED);
    if (s_ptt_button) lv_obj_add_state(s_ptt_button, LV_STATE_DISABLED);
}

} // namespace

namespace walkie_page::ui::runtime
{

bool is_available()
{
#if defined(TRAIL_MATE_PLATFORM_LINUX)
    // The page exists even when the audio/radio backend is not ready.
    // enter() reports startup errors inside the normal device UI.
    return true;
#else
    return platform::ui::walkie::is_supported();
#endif
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    s_host = host;

    if (platform::ui::device::power_tier() >= 1)
    {
        ::ui::feedback::show_notice("Low battery - audio disabled", 3000);
    }

    s_started = false;

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    if (s_root)
    {
        lv_obj_del(s_root);
        s_root = nullptr;
    }

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(kWarmBg), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_set_style_pad_row(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_root, root_key_event_cb, LV_EVENT_KEY, nullptr);

    ::ui::widgets::top_bar_init(s_top_bar, s_root);
    ::ui::widgets::top_bar_set_title(s_top_bar, ::ui::i18n::tr("Walkie Talkie"));
    ::ui::widgets::top_bar_set_back_callback(s_top_bar, on_back, nullptr);
    if (s_top_bar.back_btn)
    {
        lv_obj_add_event_cb(s_top_bar.back_btn, root_key_event_cb, LV_EVENT_KEY, nullptr);
    }
    ui_update_top_bar_battery(s_top_bar);
    if (app_g && s_top_bar.back_btn)
    {
        lv_group_remove_all_objs(app_g);
        lv_group_add_obj(app_g, s_top_bar.back_btn);
        lv_group_focus_obj(s_top_bar.back_btn);
        set_default_group(app_g);
        lv_group_set_editing(app_g, false);
    }
    else
    {
        set_default_group(prev_group);
    }

    lv_obj_t* content = lv_obj_create(s_root);
    lv_obj_set_size(content, LV_PCT(100), 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_coord_t screen_w = lv_display_get_horizontal_resolution(nullptr);
    const bool short_screen = lv_display_get_vertical_resolution(nullptr) <= 200;
    if (screen_w <= 0)
    {
        screen_w = 320;
    }
    const lv_coord_t panel_w =
        std::min<lv_coord_t>(kControlPanelMaxWidth,
                             std::max<lv_coord_t>(kControlPanelMinWidth,
                                                  screen_w - kControlPanelSideMargin));

    lv_obj_t* stack = lv_obj_create(content);
    lv_obj_set_size(stack, LV_PCT(100), short_screen ? LV_SIZE_CONTENT : LV_PCT(100));
    if (short_screen)
    {
        lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(content, LV_DIR_VER);
    }
    lv_obj_set_style_bg_opa(stack, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stack, 0, 0);
    lv_obj_set_style_pad_left(stack, 10, 0);
    lv_obj_set_style_pad_right(stack, 10, 0);
    lv_obj_set_style_pad_top(stack, short_screen ? 2 : 8, 0);
    lv_obj_set_style_pad_bottom(stack, short_screen ? 2 : 8, 0);
    lv_obj_set_style_pad_row(stack, short_screen ? 2 : 6, 0);
    lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(stack, LV_OBJ_FLAG_SCROLLABLE);

    s_freq_label = lv_label_create(stack);
    ::ui::i18n::set_label_text_raw(s_freq_label, "--.- MHz");
    lv_obj_set_style_text_font(s_freq_label, short_screen ? &lv_font_montserrat_18 : &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_freq_label, lv_color_hex(kText), 0);
    lv_obj_set_style_text_align(s_freq_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* state_row = lv_obj_create(stack);
    lv_obj_set_size(state_row, panel_w, 22);
    lv_obj_set_style_bg_opa(state_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state_row, 0, 0);
    lv_obj_set_style_pad_all(state_row, 0, 0);
    lv_obj_set_style_pad_column(state_row, 8, 0);
    lv_obj_set_flex_flow(state_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(state_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* mod_badge = lv_obj_create(state_row);
    lv_obj_set_size(mod_badge, 54, 20);
    lv_obj_set_style_bg_color(mod_badge, lv_color_hex(kAmber), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mod_badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(mod_badge, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(mod_badge, lv_color_hex(kAmberDark), LV_PART_MAIN);
    lv_obj_set_style_radius(mod_badge, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(mod_badge, 0, LV_PART_MAIN);
    lv_obj_clear_flag(mod_badge, LV_OBJ_FLAG_SCROLLABLE);

    s_mod_label = lv_label_create(mod_badge);
    ::ui::i18n::set_label_text(s_mod_label, "FSK");
    lv_obj_set_style_text_font(s_mod_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_mod_label, lv_color_hex(kText), 0);
    lv_obj_set_style_text_align(s_mod_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_mod_label);

    s_mode_label = lv_label_create(state_row);
    ::ui::i18n::set_label_text(s_mode_label, "LISTEN");
    lv_obj_set_style_text_font(s_mode_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_mode_label, lv_color_hex(kOk), 0);
    lv_obj_set_style_text_align(s_mode_label, LV_TEXT_ALIGN_CENTER, 0);

    s_control_panel = lv_obj_create(stack);
    lv_obj_set_size(s_control_panel, panel_w, LV_SIZE_CONTENT);
    style_control_panel(s_control_panel);
    if (short_screen)
    {
        lv_obj_set_style_pad_top(s_control_panel, 4, 0);
        lv_obj_set_style_pad_bottom(s_control_panel, 4, 0);
        lv_obj_set_style_pad_row(s_control_panel, 2, 0);
    }

    s_monitor_row = lv_btn_create(s_control_panel);
    lv_obj_set_size(s_monitor_row, LV_PCT(100), short_screen ? 26 : kMonitorRowHeight);
    lv_obj_set_style_pad_left(s_monitor_row, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_monitor_row, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_monitor_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_monitor_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(s_monitor_row, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_monitor_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_monitor_row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_monitor_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_monitor_row, monitor_row_event_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_monitor_row, monitor_row_event_cb, LV_EVENT_KEY, nullptr);
    style_monitor_row(s_monitor_row);

    s_monitor_label = lv_label_create(s_monitor_row);
    ::ui::i18n::set_label_text(s_monitor_label, "Monitor Off");
    lv_obj_set_style_text_font(s_monitor_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_monitor_label, lv_color_hex(kText), 0);
    lv_obj_set_flex_grow(s_monitor_label, 1);
    lv_label_set_long_mode(s_monitor_label, LV_LABEL_LONG_CLIP);

    s_monitor_switch = lv_switch_create(s_monitor_row);
    lv_obj_set_size(s_monitor_switch, kMonitorSwitchWidth, kMonitorSwitchHeight);
    style_monitor_switch(s_monitor_switch);
    lv_obj_add_event_cb(s_monitor_switch, monitor_row_event_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_monitor_switch, monitor_row_event_cb, LV_EVENT_KEY, nullptr);

    lv_obj_t* volume_row = lv_obj_create(s_control_panel);
    lv_obj_set_size(volume_row, LV_PCT(100), short_screen ? 18 : 24);
    lv_obj_set_style_bg_opa(volume_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(volume_row, 0, 0);
    lv_obj_set_style_pad_all(volume_row, 0, 0);
    lv_obj_set_style_pad_column(volume_row, 6, 0);
    lv_obj_set_flex_flow(volume_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(volume_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(volume_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(volume_row, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(volume_row, LV_OBJ_FLAG_SCROLLABLE);

    s_volume_label = lv_label_create(volume_row);
    ::ui::i18n::set_label_text(s_volume_label, "VOL 80");
    lv_obj_set_width(s_volume_label, 74);
    lv_obj_set_style_text_font(s_volume_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_volume_label, lv_color_hex(kTextDim), 0);
    lv_obj_set_style_text_align(s_volume_label, LV_TEXT_ALIGN_LEFT, 0);

    s_volume_bar = lv_bar_create(volume_row);
    lv_obj_set_size(s_volume_bar, 0, kVolumeBarHeight);
    lv_obj_set_flex_grow(s_volume_bar, 1);
    lv_bar_set_range(s_volume_bar, 0, 100);
    lv_bar_set_value(s_volume_bar, platform::ui::walkie::volume(), LV_ANIM_OFF);
    style_readonly_volume_bar(s_volume_bar);

    if (app_g && s_monitor_row)
    {
        lv_group_add_obj(app_g, s_monitor_row);
        lv_group_set_editing(app_g, false);
    }

    if (::ui::page_profile::current().large_touch_hitbox)
    {
        lv_obj_t* ptt = lv_button_create(s_control_panel);
        s_ptt_button = ptt;
        lv_obj_set_size(ptt, LV_PCT(100), 48);
        lv_obj_set_style_bg_color(ptt, lv_color_hex(kAmber), 0);
        lv_obj_set_style_bg_color(ptt, lv_color_hex(kAmberDark), LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(ptt, lv_color_hex(kLine), LV_STATE_DISABLED);
        lv_obj_set_style_bg_opa(ptt, LV_OPA_50, LV_STATE_DISABLED);
        lv_obj_set_style_text_color(ptt, lv_color_hex(kText), 0);
        lv_obj_set_style_text_color(ptt, lv_color_hex(kGray), LV_STATE_DISABLED);
        lv_obj_add_event_cb(ptt, ptt_event_cb, LV_EVENT_ALL, nullptr);
        lv_obj_t* label = lv_label_create(ptt);
        ::ui::i18n::set_label_text(label, "Hold to talk");
        lv_obj_center(label);
        if (app_g) lv_group_add_obj(app_g, ptt);
    }

#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
    if (short_screen)
    {
        auto* hint = lv_label_create(stack);
        ::ui::i18n::set_label_text(hint, "T: Talk / listen");
        lv_obj_set_style_text_font(hint, ::ui::page_profile::resolve_caption_font(), 0);
    }
#endif
    lv_obj_t* level_row = lv_obj_create(stack);
    const lv_coord_t meter_height = short_screen ? 10 : kLevelMeterHeight;
    lv_obj_set_size(level_row, panel_w, meter_height);
    lv_obj_set_style_bg_opa(level_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(level_row, 0, 0);
    lv_obj_set_style_pad_all(level_row, 0, 0);
    lv_obj_set_style_pad_column(level_row, 8, 0);
    lv_obj_set_flex_flow(level_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(level_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(level_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* vu_left = lv_obj_create(level_row);
    lv_obj_set_size(vu_left, 0, meter_height);
    lv_obj_set_flex_grow(vu_left, 1);
    lv_obj_set_style_bg_color(vu_left, lv_color_hex(kPanelBg), 0);
    lv_obj_set_style_bg_opa(vu_left, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(vu_left, 1, 0);
    lv_obj_set_style_border_color(vu_left, lv_color_hex(kLine), 0);
    lv_obj_set_style_radius(vu_left, 4, 0);
    lv_obj_set_style_pad_all(vu_left, 0, 0);
    lv_obj_clear_flag(vu_left, LV_OBJ_FLAG_SCROLLABLE);

    s_left_fill = lv_obj_create(vu_left);
    lv_obj_set_size(s_left_fill, 0, LV_PCT(100));
    lv_obj_align(s_left_fill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(s_left_fill, lv_color_hex(kOk), 0);
    lv_obj_set_style_border_width(s_left_fill, 0, 0);
    lv_obj_set_style_radius(s_left_fill, 4, 0);
    lv_obj_clear_flag(s_left_fill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* vu_right = lv_obj_create(level_row);
    lv_obj_set_size(vu_right, 0, meter_height);
    lv_obj_set_flex_grow(vu_right, 1);
    lv_obj_set_style_bg_color(vu_right, lv_color_hex(kPanelBg), 0);
    lv_obj_set_style_bg_opa(vu_right, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(vu_right, 1, 0);
    lv_obj_set_style_border_color(vu_right, lv_color_hex(kLine), 0);
    lv_obj_set_style_radius(vu_right, 4, 0);
    lv_obj_set_style_pad_all(vu_right, 0, 0);
    lv_obj_clear_flag(vu_right, LV_OBJ_FLAG_SCROLLABLE);

    s_right_fill = lv_obj_create(vu_right);
    lv_obj_set_size(s_right_fill, 0, LV_PCT(100));
    lv_obj_align(s_right_fill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(s_right_fill, lv_color_hex(kOk), 0);
    lv_obj_set_style_border_width(s_right_fill, 0, 0);
    lv_obj_set_style_radius(s_right_fill, 4, 0);
    lv_obj_clear_flag(s_right_fill, LV_OBJ_FLAG_SCROLLABLE);

    platform::ui::walkie::Status st = platform::ui::walkie::get_status();
    if (st.freq_mhz > 0.0f)
    {
        set_freq_text(st.freq_mhz);
    }

    const char* error = nullptr;

    if (!error)
    {
        s_started = platform::ui::walkie::start();
        if (!s_started)
        {
            const char* detail = platform::ui::walkie::last_error();
            if (detail && detail[0] != '\0')
            {
                error = detail;
            }
            else
            {
                error = "Walkie start failed";
            }
        }
    }

    if (!s_started)
    {
        set_error_text(error);
        return;
    }

    platform::ui::screen::disable_sleep();

    st = platform::ui::walkie::get_status();
    set_freq_text(st.freq_mhz);
    if (s_monitor_switch)
    {
        set_monitor_visual_state(st.monitor_enabled);
    }

    if (!s_timer)
    {
        s_timer = lv_timer_create(refresh_cb, 120, nullptr);
    }
    refresh_cb(nullptr);
}

void exit(lv_obj_t* parent)
{
    (void)parent;
    const bool keep_monitoring = platform::ui::walkie::monitor_enabled();
    platform::ui::walkie::set_ptt(false);
    if (s_timer)
    {
        lv_timer_del(s_timer);
        s_timer = nullptr;
    }
    if (s_root)
    {
        lv_obj_del(s_root);
        s_root = nullptr;
    }
    s_freq_label = nullptr;
    s_mod_label = nullptr;
    s_mode_label = nullptr;
    s_left_fill = nullptr;
    s_right_fill = nullptr;
    s_monitor_row = nullptr;
    s_monitor_switch = nullptr;
    s_monitor_label = nullptr;
    s_control_panel = nullptr;
    s_ptt_button = nullptr;
    s_volume_bar = nullptr;
    s_volume_label = nullptr;
    s_top_bar = {};
    s_started = false;

    if (!keep_monitoring)
    {
        platform::ui::walkie::stop();
    }
    ::ui::status::force_update();
    platform::ui::screen::enable_sleep();
    s_host = nullptr;
}

} // namespace walkie_page::ui::runtime

void ui_walkie_talkie_enter(lv_obj_t* parent)
{
    walkie_page::ui::shell::enter(nullptr, parent);
}

void ui_walkie_talkie_exit(lv_obj_t* parent)
{
    walkie_page::ui::shell::exit(nullptr, parent);
}

#else

namespace walkie_page::ui::runtime
{

bool is_available()
{
    return false;
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    (void)host;
    (void)parent;
}

void exit(lv_obj_t* parent)
{
    (void)parent;
}

} // namespace walkie_page::ui::runtime

#endif
