#include "ui_walkie_talkie.h"

#include "../walkie/walkie_service.h"
#if defined(ARDUINO_LILYGO_LORA_SX1262)
#include "../board/TLoRaPagerBoard.h"
#endif
#include "ui_common.h"
#include "widgets/top_bar.h"
#include <Arduino.h>

// Forward declarations from main.cpp
extern void disableScreenSleep();
extern void enableScreenSleep();

namespace
{
lv_obj_t* s_root = nullptr;
lv_obj_t* s_freq_label = nullptr;
lv_obj_t* s_mod_label = nullptr;
lv_obj_t* s_mode_label = nullptr;
lv_obj_t* s_left_fill = nullptr;
lv_obj_t* s_right_fill = nullptr;
lv_obj_t* s_volume_bar = nullptr;
lv_obj_t* s_volume_label = nullptr;
lv_timer_t* s_timer = nullptr;
ui::widgets::TopBar s_top_bar;
bool s_started = false;

constexpr lv_coord_t kVuWidth = 12;
constexpr lv_coord_t kVuHeight = 120;
constexpr lv_coord_t kVolumeBarWidth = 220;
constexpr lv_coord_t kVolumeBarHeight = 12;

void on_back(void*)
{
    ui_request_exit_to_menu();
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
    lv_coord_t height = lv_obj_get_height(parent);
    lv_coord_t fill_h = static_cast<lv_coord_t>((level * height) / 100);
    if (fill_h < 0)
    {
        fill_h = 0;
    }
    if (fill_h > height)
    {
        fill_h = height;
    }
    lv_obj_set_height(fill, fill_h);
}

void refresh_cb(lv_timer_t*)
{
    walkie::Status st = walkie::get_status();
    ui_update_top_bar_battery(s_top_bar);
    if (s_mode_label)
    {
        lv_label_set_text(s_mode_label, st.tx ? "TALK" : "LISTEN");
    }
    update_vu(s_left_fill, st.tx ? st.tx_level : st.rx_level);
    update_vu(s_right_fill, st.tx ? st.tx_level : st.rx_level);

    if (s_volume_bar)
    {
        int vol = walkie::get_volume();
        lv_bar_set_value(s_volume_bar, vol, LV_ANIM_OFF);
    }
    if (s_volume_label)
    {
        char buf[24];
        int vol = walkie::get_volume();
        snprintf(buf, sizeof(buf), "VOL %d", vol);
        lv_label_set_text(s_volume_label, buf);
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
    lv_label_set_text(s_freq_label, buf);
}

void set_error_text(const char* message)
{
    if (s_freq_label)
    {
        lv_label_set_text(s_freq_label, "Walkie Talkie");
    }
    if (s_mod_label)
    {
        lv_label_set_text(s_mod_label, message ? message : "Walkie not available");
    }
    if (s_mode_label)
    {
        lv_label_set_text(s_mode_label, "Press Back");
    }
    update_vu(s_left_fill, 0);
    update_vu(s_right_fill, 0);
}
} // namespace

void ui_walkie_talkie_enter(lv_obj_t* parent)
{
    s_started = false;

    if (s_root)
    {
        lv_obj_del(s_root);
        s_root = nullptr;
    }

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(s_root, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_set_style_pad_row(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    ::ui::widgets::top_bar_init(s_top_bar, s_root);
    ::ui::widgets::top_bar_set_title(s_top_bar, "Walkie Talkie");
    ::ui::widgets::top_bar_set_back_callback(s_top_bar, on_back, nullptr);
    ui_update_top_bar_battery(s_top_bar);

    lv_obj_t* content = lv_obj_create(s_root);
    lv_obj_set_size(content, LV_PCT(100), 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* stack = lv_obj_create(content);
    lv_obj_set_size(stack, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(stack, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stack, 0, 0);
    lv_obj_set_style_pad_all(stack, 0, 0);
    lv_obj_set_style_pad_row(stack, 6, 0);
    lv_obj_center(stack);

    s_freq_label = lv_label_create(stack);
    lv_obj_set_style_text_font(s_freq_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(s_freq_label, LV_TEXT_ALIGN_CENTER, 0);

    s_mod_label = lv_label_create(stack);
    lv_label_set_text(s_mod_label, "FSK");
    lv_obj_set_style_text_font(s_mod_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(s_mod_label, LV_TEXT_ALIGN_CENTER, 0);

    s_mode_label = lv_label_create(stack);
    lv_label_set_text(s_mode_label, "LISTEN");
    lv_obj_set_style_text_font(s_mode_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(s_mode_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* vu_left = lv_obj_create(content);
    lv_obj_set_size(vu_left, kVuWidth, kVuHeight);
    lv_obj_align(vu_left, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_set_style_bg_opa(vu_left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vu_left, 1, 0);
    lv_obj_set_style_border_color(vu_left, lv_color_black(), 0);
    lv_obj_set_style_pad_all(vu_left, 0, 0);
    lv_obj_clear_flag(vu_left, LV_OBJ_FLAG_SCROLLABLE);

    s_left_fill = lv_obj_create(vu_left);
    lv_obj_set_width(s_left_fill, LV_PCT(100));
    lv_obj_set_height(s_left_fill, 0);
    lv_obj_align(s_left_fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_left_fill, lv_color_hex(0x2E7D32), 0);
    lv_obj_set_style_border_width(s_left_fill, 0, 0);
    lv_obj_clear_flag(s_left_fill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* vu_right = lv_obj_create(content);
    lv_obj_set_size(vu_right, kVuWidth, kVuHeight);
    lv_obj_align(vu_right, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_set_style_bg_opa(vu_right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vu_right, 1, 0);
    lv_obj_set_style_border_color(vu_right, lv_color_black(), 0);
    lv_obj_set_style_pad_all(vu_right, 0, 0);
    lv_obj_clear_flag(vu_right, LV_OBJ_FLAG_SCROLLABLE);

    s_right_fill = lv_obj_create(vu_right);
    lv_obj_set_width(s_right_fill, LV_PCT(100));
    lv_obj_set_height(s_right_fill, 0);
    lv_obj_align(s_right_fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_right_fill, lv_color_hex(0x2E7D32), 0);
    lv_obj_set_style_border_width(s_right_fill, 0, 0);
    lv_obj_clear_flag(s_right_fill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* vol_container = lv_obj_create(content);
    lv_obj_set_size(vol_container, kVolumeBarWidth, LV_SIZE_CONTENT);
    lv_obj_align(vol_container, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_bg_opa(vol_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vol_container, 0, 0);
    lv_obj_set_style_pad_all(vol_container, 0, 0);
    lv_obj_set_style_pad_row(vol_container, 4, 0);
    lv_obj_set_flex_flow(vol_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(vol_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(vol_container, LV_OBJ_FLAG_SCROLLABLE);

    s_volume_label = lv_label_create(vol_container);
    lv_label_set_text(s_volume_label, "VOL 80");
    lv_obj_set_style_text_font(s_volume_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_volume_label, LV_TEXT_ALIGN_CENTER, 0);

    s_volume_bar = lv_bar_create(vol_container);
    lv_obj_set_size(s_volume_bar, kVolumeBarWidth, kVolumeBarHeight);
    lv_bar_set_range(s_volume_bar, 0, 100);
    lv_bar_set_value(s_volume_bar, walkie::get_volume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_volume_bar, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_volume_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_volume_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_volume_bar, lv_color_hex(0x2E7D32), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_volume_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_volume_bar, 4, LV_PART_INDICATOR);

    walkie::Status st = walkie::get_status();
    if (st.freq_mhz > 0.0f)
    {
        set_freq_text(st.freq_mhz);
    }

    const char* error = nullptr;
#if defined(ARDUINO_LILYGO_LORA_SX1262)
    TLoRaPagerBoard* board = TLoRaPagerBoard::getInstance();
    if (!board)
    {
        error = "Board not ready";
    }
    else if (!board->isRadioOnline())
    {
        error = "Radio not ready";
    }
    else if (!(board->getDevicesProbe() & HW_CODEC_ONLINE))
    {
        error = "Audio codec not ready";
    }
#endif

    if (!error)
    {
        s_started = walkie::start();
        if (!s_started)
        {
            const char* detail = walkie::get_last_error();
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

    disableScreenSleep();

    st = walkie::get_status();
    set_freq_text(st.freq_mhz);

    if (!s_timer)
    {
        s_timer = lv_timer_create(refresh_cb, 120, nullptr);
    }
    refresh_cb(nullptr);
}

void ui_walkie_talkie_exit(lv_obj_t* parent)
{
    (void)parent;
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
    s_volume_bar = nullptr;
    s_volume_label = nullptr;
    s_top_bar = {};
    s_started = false;

    walkie::stop();
    enableScreenSleep();
}
