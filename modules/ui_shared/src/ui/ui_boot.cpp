/**
 * @file ui_boot.cpp
 * @brief Boot splash screen implementation.
 */

#include "ui/ui_boot.h"

#include <cstring>

#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif

namespace
{

constexpr uint32_t kFadeMs = 900;
#if defined(TRAIL_MATE_BOOT_MIN_SHOW_MS)
constexpr uint32_t kMinShowMs = TRAIL_MATE_BOOT_MIN_SHOW_MS;
#elif defined(TRAIL_MATE_ESP_BOARD_TAB5)
constexpr uint32_t kMinShowMs = 900;
#else
constexpr uint32_t kMinShowMs = 3000;
#endif
constexpr uint32_t kBootBgColor = 0xF6E6C6;
constexpr uint32_t kBootLogTextColor = 0x8A6A3A;
constexpr lv_coord_t kBootLogPadX = 10;
constexpr lv_coord_t kBootLogPadBottom = 8;
constexpr std::size_t kBootLogTextCapacity = 96;

lv_obj_t* s_root = nullptr;
lv_obj_t* s_logo = nullptr;
lv_obj_t* s_log_label = nullptr;
lv_timer_t* s_gate_timer = nullptr;
uint32_t s_start_ms = 0;
bool s_ready = false;
char s_log_text[kBootLogTextCapacity] = "";

extern "C"
{
    extern const lv_image_dsc_t logo;
}

void set_logo_opa(lv_obj_t* obj, int32_t v)
{
    if (!obj) return;
    lv_obj_set_style_img_opa(obj, static_cast<lv_opa_t>(v), LV_PART_MAIN);
}

void cleanup()
{
    if (s_gate_timer)
    {
        lv_timer_del(s_gate_timer);
        s_gate_timer = nullptr;
    }

    if (s_root)
    {
        lv_obj_del(s_root);
        s_root = nullptr;
        s_logo = nullptr;
        s_log_label = nullptr;
    }

    s_ready = false;
    s_start_ms = 0;
    s_log_text[0] = '\0';
}

void check_gate()
{
    if (!s_root || !s_ready)
    {
        return;
    }

    uint32_t elapsed = lv_tick_elaps(s_start_ms);
    if (elapsed < kMinShowMs)
    {
        return;
    }

    cleanup();
}

void refresh_log_label()
{
    if (!s_log_label)
    {
        return;
    }

    lv_label_set_text(s_log_label, s_log_text);
    lv_refr_now(nullptr);
}

} // namespace

namespace ui::boot
{

void show()
{
    if (s_root)
    {
        return;
    }

    lv_obj_t* parent = lv_layer_top();
    if (!parent)
    {
        return;
    }

    s_ready = false;
    s_start_ms = lv_tick_get();

    s_root = lv_obj_create(parent);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(kBootBgColor), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_coord_t screen_w = lv_display_get_physical_horizontal_resolution(NULL);
    lv_coord_t screen_h = lv_display_get_physical_vertical_resolution(NULL);
    lv_obj_set_size(s_root, screen_w, screen_h);
    lv_obj_set_pos(s_root, 0, 0);

    s_logo = lv_image_create(s_root);
    lv_image_set_src(s_logo, &logo);
    lv_obj_center(s_logo);
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    lv_obj_set_style_img_opa(s_logo, LV_OPA_COVER, LV_PART_MAIN);
#else
    lv_obj_set_style_img_opa(s_logo, LV_OPA_0, LV_PART_MAIN);
#endif

    lv_obj_move_foreground(s_root);

#if !defined(TRAIL_MATE_ESP_BOARD_TAB5)
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_logo);
    lv_anim_set_values(&anim, LV_OPA_0, LV_OPA_COVER);
    lv_anim_set_time(&anim, kFadeMs);
    lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)set_logo_opa);
    lv_anim_start(&anim);
#endif

    s_log_label = lv_label_create(s_root);
    lv_obj_set_width(s_log_label, screen_w - (kBootLogPadX * 2));
    lv_obj_set_style_text_color(s_log_label, lv_color_hex(kBootLogTextColor), 0);
    lv_obj_set_style_text_font(s_log_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(s_log_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_log_label, LV_LABEL_LONG_DOT);
    lv_obj_align(s_log_label, LV_ALIGN_BOTTOM_LEFT, kBootLogPadX, -kBootLogPadBottom);
    lv_label_set_text(s_log_label, s_log_text);

    s_gate_timer = lv_timer_create(
        [](lv_timer_t*)
        {
            check_gate();
        },
        50, nullptr);
    lv_timer_set_repeat_count(s_gate_timer, -1);
}

void set_log_line(const char* text)
{
    const char* value = text ? text : "";
    if (std::strcmp(s_log_text, value) == 0)
    {
        return;
    }

    std::strncpy(s_log_text, value, kBootLogTextCapacity - 1);
    s_log_text[kBootLogTextCapacity - 1] = '\0';
    refresh_log_label();
}

void mark_ready()
{
    s_ready = true;
    check_gate();
}

} // namespace ui::boot
