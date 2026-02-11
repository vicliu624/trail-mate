#include "ui_sstv.h"

#include "../board/BoardBase.h"
#include "../sstv/sstv_service.h"
#include "ui_common.h"
#include <Arduino.h>
#include <cmath>
#include <cstring>

// Forward declarations from main.cpp
extern void disableScreenSleep();
extern void enableScreenSleep();

namespace
{
constexpr lv_coord_t kScreenW = 480;
constexpr lv_coord_t kScreenH = 222;
constexpr lv_coord_t kTopBarHeight = ::ui::widgets::kTopBarHeight;
constexpr lv_coord_t kMainHeight = 192;
constexpr lv_coord_t kPadding = 8;

constexpr lv_coord_t kImgW = 288;
constexpr lv_coord_t kImgH = 192;
constexpr lv_coord_t kImgX = kPadding;
constexpr lv_coord_t kImgY = 0;

constexpr lv_coord_t kInfoX = kImgX + kImgW + kPadding;
constexpr lv_coord_t kInfoW = 168;
constexpr lv_coord_t kInfoH = 192;
constexpr lv_coord_t kInfoTextW = 140;

constexpr lv_coord_t kProgressH = 8;
constexpr lv_coord_t kProgressY = kMainHeight - 14;
constexpr lv_coord_t kProgressW = kInfoW;

constexpr lv_coord_t kMeterX = 136;
constexpr lv_coord_t kMeterY = 34;
constexpr lv_coord_t kMeterW = 32;
constexpr lv_coord_t kMeterH = 120;
constexpr lv_coord_t kMeterSegH = 8;
constexpr lv_coord_t kMeterSegGap = 2;
constexpr int kMeterSegments = 12;
constexpr lv_coord_t kMetricsX = 0;
constexpr lv_coord_t kMetricsY = 34;
constexpr lv_coord_t kMetricsLineGap = 20;

constexpr uint32_t kColorWarmBg = 0xF6E6C6;
constexpr uint32_t kColorAccent = 0xEBA341;
constexpr uint32_t kColorPanelBg = 0xFAF0D8;
constexpr uint32_t kColorLine = 0xE7C98F;
constexpr uint32_t kColorText = 0x6B4A1E;
constexpr uint32_t kColorTextDim = 0x8A6A3A;
constexpr uint32_t kColorOk = 0x3E7D3E;
constexpr uint32_t kColorWarn = 0xB94A2C;
constexpr uint32_t kColorGray = 0x6E6E6E;
constexpr uint32_t kColorMeterMid = 0xC18B2C;

struct SstvUi
{
    lv_obj_t* root = nullptr;
    ui::widgets::TopBar top_bar = {};
    lv_obj_t* img_box = nullptr;
    lv_obj_t* img = nullptr;
    lv_obj_t* img_placeholder = nullptr;
    lv_obj_t* info_area = nullptr;
    lv_obj_t* label_state_sub = nullptr;
    lv_obj_t* label_metric_sync = nullptr;
    lv_obj_t* label_metric_slant = nullptr;
    lv_obj_t* label_metric_level = nullptr;
    lv_obj_t* label_mode = nullptr;
    lv_obj_t* label_ready = nullptr;
    lv_obj_t* progress = nullptr;
    lv_obj_t* meter_box = nullptr;
    lv_obj_t* meter_segments[kMeterSegments] = {};
};

SstvUi s_ui;
lv_timer_t* s_refresh_timer = nullptr;
int s_last_meter_active = -1;
sstv::State s_last_state = sstv::State::Idle;
uint16_t s_last_line = 0;
bool s_last_sync_lock = false;
int s_last_level_pct = -1;
char s_last_mode[24] = "";
lv_image_dsc_t s_frame_dsc = {};
bool s_frame_ready = false;

void ensure_frame_dsc()
{
    if (s_frame_ready)
    {
        return;
    }
    const uint16_t* frame = sstv::get_framebuffer();
    if (!frame)
    {
        return;
    }
    s_frame_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_frame_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_frame_dsc.header.flags = 0;
    s_frame_dsc.header.w = sstv::frame_width();
    s_frame_dsc.header.h = sstv::frame_height();
    s_frame_dsc.header.stride = static_cast<uint32_t>(s_frame_dsc.header.w) * 2;
    s_frame_dsc.data_size =
        static_cast<uint32_t>(s_frame_dsc.header.w) * static_cast<uint32_t>(s_frame_dsc.header.h) * 2;
    s_frame_dsc.data = reinterpret_cast<const uint8_t*>(frame);
    s_frame_ready = true;
}

void on_back(void*)
{
    ui_request_exit_to_menu();
}

void root_key_event_cb(lv_event_t* e)
{
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_BACKSPACE)
    {
        return;
    }
    on_back(nullptr);
}

void update_battery_labels()
{
    if (!s_ui.top_bar.right_label)
    {
        return;
    }
    ui_update_top_bar_battery(s_ui.top_bar);
}

void refresh_cb(lv_timer_t*)
{
    update_battery_labels();
    sstv::Status st = sstv::get_status();
    ui_sstv_set_audio_level(st.audio_level);

    bool sync_lock = (st.state == sstv::State::Receiving);
    if (s_ui.label_metric_sync && sync_lock != s_last_sync_lock)
    {
        lv_label_set_text(s_ui.label_metric_sync, sync_lock ? "SYNC: LOCK" : "SYNC: --");
        s_last_sync_lock = sync_lock;
    }
    if (s_ui.label_metric_level)
    {
        int level_pct = static_cast<int>(st.audio_level * 100.0f + 0.5f);
        if (level_pct != s_last_level_pct)
        {
            char buf[20];
            snprintf(buf, sizeof(buf), "LEVEL: %d%%", level_pct);
            lv_label_set_text(s_ui.label_metric_level, buf);
            s_last_level_pct = level_pct;
        }
    }

    const char* mode = sstv::get_mode_name();
    if (!mode || mode[0] == '\0' || strcmp(mode, "Unknown") == 0 || st.state == sstv::State::Waiting)
    {
        mode = "Auto";
    }
    if (s_ui.label_mode && strcmp(mode, s_last_mode) != 0)
    {
        ui_sstv_set_mode(mode);
        snprintf(s_last_mode, sizeof(s_last_mode), "%s", mode);
    }

    if (st.state != s_last_state)
    {
        s_last_state = st.state;
        if (st.state == sstv::State::Waiting)
        {
            ui_sstv_set_state(SSTV_STATE_WAITING);
        }
        else if (st.state == sstv::State::Receiving)
        {
            ui_sstv_set_state(SSTV_STATE_RECEIVING);
        }
        else if (st.state == sstv::State::Complete)
        {
            ui_sstv_set_state(SSTV_STATE_COMPLETE);
        }
        else if (st.state == sstv::State::Error)
        {
            if (s_ui.label_state_sub)
            {
                const char* err = sstv::get_last_error();
                lv_label_set_text(s_ui.label_state_sub, err ? err : "Decoder error");
            }
            if (s_ui.label_ready)
            {
                lv_label_set_text(s_ui.label_ready, "ERROR");
                lv_obj_set_style_text_color(s_ui.label_ready, lv_color_hex(kColorWarn), 0);
            }
        }
    }

    if (st.state == sstv::State::Receiving)
    {
        if (st.line != s_last_line && s_ui.label_state_sub)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "Decoding line: %u/256",
                     static_cast<unsigned>(st.line));
            lv_label_set_text(s_ui.label_state_sub, buf);
        }
        ui_sstv_set_progress(st.progress);
    }
    else if (st.state == sstv::State::Complete)
    {
        ui_sstv_set_progress(1.0f);
        if (s_ui.label_state_sub)
        {
            const char* saved = sstv::get_last_saved_path();
            if (saved && saved[0] != '\0')
            {
                char buf[80];
                snprintf(buf, sizeof(buf), "Saved: %s", saved);
                lv_label_set_text(s_ui.label_state_sub, buf);
            }
        }
    }

    if (st.state == sstv::State::Receiving || st.state == sstv::State::Complete)
    {
        if (st.has_image)
        {
            ensure_frame_dsc();
            if (s_frame_ready && s_ui.img)
            {
                ui_sstv_set_image(&s_frame_dsc);
                if (st.line != s_last_line)
                {
                    lv_obj_invalidate(s_ui.img);
                }
            }
        }
    }
    else if (st.state == sstv::State::Waiting)
    {
        ui_sstv_set_image(nullptr);
    }
    if (st.line != s_last_line)
    {
        s_last_line = st.line;
    }
}

void apply_label_style(lv_obj_t* label, const lv_font_t* font, uint32_t color)
{
    if (!label)
    {
        return;
    }
    if (font)
    {
        lv_obj_set_style_text_font(label, font, 0);
    }
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

void build_top_bar(lv_obj_t* parent)
{
    ::ui::widgets::TopBarConfig cfg;
    cfg.height = ::ui::widgets::kTopBarHeight;
    ::ui::widgets::top_bar_init(s_ui.top_bar, parent, cfg);
    ::ui::widgets::top_bar_set_title(s_ui.top_bar, "SSTV RECEIVER");
    ::ui::widgets::top_bar_set_back_callback(
        s_ui.top_bar, [](void*)
        { on_back(nullptr); },
        nullptr);
    if (s_ui.top_bar.container)
    {
        lv_obj_set_pos(s_ui.top_bar.container, 0, 0);
    }
    if (s_ui.top_bar.back_btn)
    {
        lv_obj_add_event_cb(s_ui.top_bar.back_btn, root_key_event_cb, LV_EVENT_KEY, nullptr);
    }
    update_battery_labels();
}

void build_main_area(lv_obj_t* parent)
{
    lv_obj_t* main = lv_obj_create(parent);
    lv_obj_set_size(main, kScreenW, kMainHeight);
    lv_obj_set_pos(main, 0, kTopBarHeight);
    lv_obj_set_style_bg_opa(main, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main, 0, 0);
    lv_obj_set_style_pad_all(main, 0, 0);
    lv_obj_clear_flag(main, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.progress = lv_bar_create(main);
    lv_obj_set_size(s_ui.progress, kProgressW, kProgressH);
    lv_obj_set_pos(s_ui.progress, kInfoX, kProgressY);
    lv_bar_set_range(s_ui.progress, 0, 100);
    lv_bar_set_value(s_ui.progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ui.progress, lv_color_hex(kColorLine), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.progress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.progress, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.progress, lv_color_hex(kColorAccent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ui.progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_ui.progress, 4, LV_PART_INDICATOR);

    s_ui.img_box = lv_obj_create(main);
    lv_obj_set_size(s_ui.img_box, kImgW, kImgH);
    lv_obj_set_pos(s_ui.img_box, kImgX, kImgY);
    lv_obj_set_style_bg_color(s_ui.img_box, lv_color_hex(kColorPanelBg), 0);
    lv_obj_set_style_bg_opa(s_ui.img_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.img_box, 2, 0);
    lv_obj_set_style_border_color(s_ui.img_box, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(s_ui.img_box, 8, 0);
    lv_obj_set_style_pad_all(s_ui.img_box, 0, 0);
    lv_obj_clear_flag(s_ui.img_box, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.img = lv_image_create(s_ui.img_box);
    lv_obj_center(s_ui.img);
    lv_obj_add_flag(s_ui.img, LV_OBJ_FLAG_HIDDEN);

    s_ui.img_placeholder = lv_label_create(s_ui.img_box);
    lv_label_set_text(s_ui.img_placeholder, "No image");
    lv_obj_center(s_ui.img_placeholder);
    apply_label_style(s_ui.img_placeholder, &lv_font_montserrat_12, kColorTextDim);

    s_ui.info_area = lv_obj_create(main);
    lv_obj_set_size(s_ui.info_area, kInfoW, kInfoH);
    lv_obj_set_pos(s_ui.info_area, kInfoX, kImgY);
    lv_obj_set_style_bg_opa(s_ui.info_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.info_area, 0, 0);
    lv_obj_set_style_pad_all(s_ui.info_area, 0, 0);
    lv_obj_clear_flag(s_ui.info_area, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.label_state_sub = lv_label_create(s_ui.info_area);
    lv_obj_set_pos(s_ui.label_state_sub, 0, 6);
    lv_obj_set_width(s_ui.label_state_sub, kInfoTextW);
    lv_obj_set_style_text_align(s_ui.label_state_sub, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_ui.label_state_sub, LV_LABEL_LONG_WRAP);
    apply_label_style(s_ui.label_state_sub, &lv_font_montserrat_14, kColorTextDim);

    s_ui.label_metric_sync = lv_label_create(s_ui.info_area);
    lv_obj_set_pos(s_ui.label_metric_sync, kMetricsX, kMetricsY);
    lv_obj_set_width(s_ui.label_metric_sync, kInfoTextW);
    lv_obj_set_style_text_align(s_ui.label_metric_sync, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_ui.label_metric_sync, LV_LABEL_LONG_WRAP);
    apply_label_style(s_ui.label_metric_sync, &lv_font_montserrat_14, kColorTextDim);
    lv_label_set_text(s_ui.label_metric_sync, "SYNC: --");

    s_ui.label_metric_slant = lv_label_create(s_ui.info_area);
    lv_obj_set_pos(s_ui.label_metric_slant, kMetricsX, kMetricsY + kMetricsLineGap);
    lv_obj_set_width(s_ui.label_metric_slant, kInfoTextW);
    lv_obj_set_style_text_align(s_ui.label_metric_slant, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_ui.label_metric_slant, LV_LABEL_LONG_WRAP);
    apply_label_style(s_ui.label_metric_slant, &lv_font_montserrat_14, kColorTextDim);
    lv_label_set_text(s_ui.label_metric_slant, "SLANT: --");

    s_ui.label_metric_level = lv_label_create(s_ui.info_area);
    lv_obj_set_pos(s_ui.label_metric_level, kMetricsX, kMetricsY + 2 * kMetricsLineGap);
    lv_obj_set_width(s_ui.label_metric_level, kInfoTextW);
    lv_obj_set_style_text_align(s_ui.label_metric_level, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_ui.label_metric_level, LV_LABEL_LONG_WRAP);
    apply_label_style(s_ui.label_metric_level, &lv_font_montserrat_14, kColorTextDim);
    lv_label_set_text(s_ui.label_metric_level, "LEVEL: 0%");

    s_ui.label_mode = lv_label_create(s_ui.info_area);
    lv_obj_set_pos(s_ui.label_mode, 0, 106);
    lv_obj_set_width(s_ui.label_mode, kInfoTextW);
    lv_obj_set_style_text_align(s_ui.label_mode, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_ui.label_mode, LV_LABEL_LONG_WRAP);
    apply_label_style(s_ui.label_mode, &lv_font_montserrat_14, kColorTextDim);

    s_ui.label_ready = lv_label_create(s_ui.info_area);
    lv_obj_set_pos(s_ui.label_ready, 0, 142);
    lv_obj_set_width(s_ui.label_ready, kInfoTextW);
    lv_obj_set_style_text_align(s_ui.label_ready, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_ui.label_ready, LV_LABEL_LONG_WRAP);
    apply_label_style(s_ui.label_ready, &lv_font_montserrat_14, kColorText);

    s_ui.meter_box = lv_obj_create(s_ui.info_area);
    lv_obj_set_size(s_ui.meter_box, kMeterW, kMeterH);
    lv_obj_set_pos(s_ui.meter_box, kMeterX, kMeterY);
    lv_obj_set_style_bg_opa(s_ui.meter_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.meter_box, 1, 0);
    lv_obj_set_style_border_color(s_ui.meter_box, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_radius(s_ui.meter_box, 2, 0);
    lv_obj_set_style_pad_all(s_ui.meter_box, 0, 0);
    lv_obj_clear_flag(s_ui.meter_box, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < kMeterSegments; ++i)
    {
        lv_obj_t* seg = lv_obj_create(s_ui.meter_box);
        lv_obj_set_size(seg, kMeterW - 4, kMeterSegH);
        lv_coord_t y = kMeterH - 2 - kMeterSegH - (i * (kMeterSegH + kMeterSegGap));
        lv_obj_set_pos(seg, 2, y);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_radius(seg, 2, 0);
        lv_obj_set_style_bg_color(seg, lv_color_hex(kColorLine), 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_40, 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        s_ui.meter_segments[i] = seg;
    }

    if (s_ui.progress)
    {
        lv_obj_move_foreground(s_ui.progress);
    }
}

void reset_ui_pointers()
{
    s_ui = {};
    s_last_meter_active = -1;
    s_last_state = sstv::State::Idle;
    s_last_line = 0;
    s_last_sync_lock = false;
    s_last_level_pct = -1;
    s_last_mode[0] = '\0';
    s_frame_ready = false;
}

} // namespace

lv_obj_t* ui_sstv_create(lv_obj_t* parent)
{
    if (!parent)
    {
        return nullptr;
    }
    if (s_ui.root)
    {
        lv_obj_del(s_ui.root);
        reset_ui_pointers();
    }

    s_ui.root = lv_obj_create(parent);
    lv_obj_set_size(s_ui.root, kScreenW, kScreenH);
    lv_obj_set_style_bg_color(s_ui.root, lv_color_hex(kColorWarmBg), 0);
    lv_obj_set_style_bg_opa(s_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.root, 0, 0);
    lv_obj_set_style_pad_all(s_ui.root, 0, 0);
    lv_obj_clear_flag(s_ui.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ui.root, root_key_event_cb, LV_EVENT_KEY, nullptr);

    build_top_bar(s_ui.root);
    build_main_area(s_ui.root);

    ui_sstv_set_state(SSTV_STATE_WAITING);
    ui_sstv_set_mode("Auto");
    ui_sstv_set_progress(0.0f);
    ui_sstv_set_audio_level(0.0f);

    update_battery_labels();
    return s_ui.root;
}

void ui_sstv_enter(lv_obj_t* parent)
{
    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    ui_sstv_create(parent);

    extern lv_group_t* app_g;
    if (app_g && s_ui.top_bar.back_btn)
    {
        lv_group_remove_all_objs(app_g);
        lv_group_add_obj(app_g, s_ui.top_bar.back_btn);
        lv_group_focus_obj(s_ui.top_bar.back_btn);
        set_default_group(app_g);
        lv_group_set_editing(app_g, false);
    }
    else
    {
        set_default_group(prev_group);
    }

    bool start_ok = sstv::start();
    if (!start_ok && s_ui.label_state_sub)
    {
        const char* err = sstv::get_last_error();
        lv_label_set_text(s_ui.label_state_sub, err ? err : "SSTV start failed");
    }

    disableScreenSleep();

    if (!s_refresh_timer)
    {
        s_refresh_timer = lv_timer_create(refresh_cb, 120, nullptr);
    }
    refresh_cb(nullptr);
}

void ui_sstv_exit(lv_obj_t* parent)
{
    (void)parent;
    if (s_refresh_timer)
    {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = nullptr;
    }
    sstv::stop();
    enableScreenSleep();

    if (s_ui.root)
    {
        lv_obj_del(s_ui.root);
        reset_ui_pointers();
    }
}

void ui_sstv_set_state(SstvState state)
{
    if (!s_ui.label_state_sub || !s_ui.label_ready)
    {
        return;
    }

    switch (state)
    {
    case SSTV_STATE_WAITING:
        lv_label_set_text(s_ui.label_state_sub, "Listening for SSTV signal...");
        lv_label_set_text(s_ui.label_ready, "SSTV RX READY");
        lv_obj_set_style_text_color(s_ui.label_ready, lv_color_hex(kColorText), 0);
        ui_sstv_set_image(nullptr);
        ui_sstv_set_progress(0.0f);
        break;
    case SSTV_STATE_RECEIVING:
        lv_label_set_text(s_ui.label_state_sub, "Decoding line: 0/256");
        lv_label_set_text(s_ui.label_ready, "RECEIVING");
        lv_obj_set_style_text_color(s_ui.label_ready, lv_color_hex(kColorOk), 0);
        break;
    case SSTV_STATE_COMPLETE:
        lv_label_set_text(s_ui.label_state_sub, "Image received");
        lv_label_set_text(s_ui.label_ready, "COMPLETE");
        lv_obj_set_style_text_color(s_ui.label_ready, lv_color_hex(kColorOk), 0);
        ui_sstv_set_progress(1.0f);
        break;
    default:
        break;
    }
}

void ui_sstv_set_mode(const char* mode_str)
{
    if (!s_ui.label_mode)
    {
        return;
    }
    char buf[48];
    if (!mode_str || mode_str[0] == '\0')
    {
        snprintf(buf, sizeof(buf), "MODE: Auto");
    }
    else
    {
        snprintf(buf, sizeof(buf), "MODE: %s", mode_str);
    }
    lv_label_set_text(s_ui.label_mode, buf);
}

void ui_sstv_set_audio_level(float level_0_1)
{
    if (!s_ui.meter_segments[0])
    {
        return;
    }
    if (level_0_1 < 0.0f)
    {
        level_0_1 = 0.0f;
    }
    if (level_0_1 > 1.0f)
    {
        level_0_1 = 1.0f;
    }
    int active = static_cast<int>(lroundf(level_0_1 * kMeterSegments));
    if (active < 0)
    {
        active = 0;
    }
    if (active > kMeterSegments)
    {
        active = kMeterSegments;
    }
    if (active == s_last_meter_active)
    {
        return;
    }
    s_last_meter_active = active;

    for (int i = 0; i < kMeterSegments; ++i)
    {
        lv_obj_t* seg = s_ui.meter_segments[i];
        if (!seg)
        {
            continue;
        }
        bool on = (i < active);
        uint32_t color = kColorLine;
        if (i >= 8)
        {
            color = kColorWarn;
        }
        else if (i >= 4)
        {
            color = kColorMeterMid;
        }
        else
        {
            color = kColorOk;
        }
        if (on)
        {
            lv_obj_set_style_bg_color(seg, lv_color_hex(color), 0);
            lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        }
        else
        {
            lv_obj_set_style_bg_color(seg, lv_color_hex(kColorLine), 0);
            lv_obj_set_style_bg_opa(seg, LV_OPA_40, 0);
        }
    }
}

void ui_sstv_set_progress(float p_0_1)
{
    if (!s_ui.progress)
    {
        return;
    }
    if (p_0_1 < 0.0f)
    {
        p_0_1 = 0.0f;
    }
    if (p_0_1 > 1.0f)
    {
        p_0_1 = 1.0f;
    }
    int value = static_cast<int>(lroundf(p_0_1 * 100.0f));
    lv_bar_set_value(s_ui.progress, value, LV_ANIM_OFF);
}

void ui_sstv_set_image(const void* img_src_or_lv_img_dsc)
{
    if (!s_ui.img)
    {
        return;
    }
    if (img_src_or_lv_img_dsc)
    {
        lv_image_set_src(s_ui.img, img_src_or_lv_img_dsc);
        lv_obj_center(s_ui.img);
        lv_obj_clear_flag(s_ui.img, LV_OBJ_FLAG_HIDDEN);
        if (s_ui.img_placeholder)
        {
            lv_obj_add_flag(s_ui.img_placeholder, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else
    {
        lv_obj_add_flag(s_ui.img, LV_OBJ_FLAG_HIDDEN);
        if (s_ui.img_placeholder)
        {
            lv_obj_clear_flag(s_ui.img_placeholder, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
