/**
 * @file ui_boot.cpp
 * @brief Boot splash screen implementation.
 */

#include "ui/ui_boot.h"

#include <cstring>
#include <string>

#include "ui/presentation/boot_splash_layout.h"
#include "ui/theme/theme_registry.h"
#include "ui/ui_theme.h"

#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_24) || !LV_FONT_MONTSERRAT_24
#define lv_font_montserrat_24 lv_font_montserrat_18
#endif

namespace
{

constexpr uint32_t kFadeMs = 900;
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
constexpr uint32_t kMinShowMs = 900;
#else
constexpr uint32_t kMinShowMs = 3000;
#endif
constexpr lv_coord_t kBootLogPadX = 10;
constexpr lv_coord_t kBootLogPadBottom = 8;
constexpr std::size_t kBootLogTextCapacity = 96;

lv_obj_t* s_root = nullptr;
lv_obj_t* s_logo = nullptr;
lv_obj_t* s_log_label = nullptr;
lv_timer_t* s_gate_timer = nullptr;
uint32_t s_start_ms = 0;
bool s_ready = false;
bool s_logo_is_image = false;
char s_log_text[kBootLogTextCapacity] = "";
std::string s_logo_path;

void set_logo_opa(lv_obj_t* obj, int32_t v)
{
    if (!obj) return;
    if (s_logo_is_image)
    {
        lv_obj_set_style_img_opa(obj, static_cast<lv_opa_t>(v), LV_PART_MAIN);
    }
    else
    {
        lv_obj_set_style_text_opa(obj, static_cast<lv_opa_t>(v), LV_PART_MAIN);
    }
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

    s_logo_is_image = false;
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

    lv_coord_t screen_w = lv_display_get_physical_horizontal_resolution(NULL);
    lv_coord_t screen_h = lv_display_get_physical_vertical_resolution(NULL);
    ui::presentation::boot_splash_layout::RootSpec root_spec{};
    root_spec.width = screen_w;
    root_spec.height = screen_h;
    root_spec.bg_hex = lv_color_to_u32(::ui::theme::page_bg());
    s_root = ui::presentation::boot_splash_layout::create_root(parent, root_spec);

    s_logo_path.clear();
    const bool has_external_logo =
        ::ui::theme::resolve_asset_path(::ui::theme::asset_slot_id(::ui::theme::AssetSlot::BootLogo),
                                        s_logo_path);
    const lv_image_dsc_t* logo_asset = ::ui::theme::builtin_asset(::ui::theme::AssetSlot::BootLogo);
    if (has_external_logo || logo_asset)
    {
        s_logo = lv_image_create(s_root);
        if (has_external_logo)
        {
            lv_image_set_src(s_logo, s_logo_path.c_str());
        }
        else
        {
            lv_image_set_src(s_logo, logo_asset);
        }
        ui::presentation::boot_splash_layout::align_hero(s_logo);
        s_logo_is_image = true;
    }
    else
    {
        s_logo = lv_label_create(s_root);
        lv_label_set_text(s_logo, "Trail Mate");
        lv_obj_set_style_text_font(s_logo, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(s_logo, ::ui::theme::text_muted(), 0);
        ui::presentation::boot_splash_layout::align_hero(s_logo);
        s_logo_is_image = false;
    }
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    if (s_logo)
    {
        if (s_logo_is_image)
        {
            lv_obj_set_style_img_opa(s_logo, LV_OPA_COVER, LV_PART_MAIN);
        }
        else
        {
            lv_obj_set_style_text_opa(s_logo, LV_OPA_COVER, LV_PART_MAIN);
        }
    }
#else
    if (s_logo)
    {
        if (s_logo_is_image)
        {
            lv_obj_set_style_img_opa(s_logo, LV_OPA_0, LV_PART_MAIN);
        }
        else
        {
            lv_obj_set_style_text_opa(s_logo, LV_OPA_0, LV_PART_MAIN);
        }
    }
#endif

    lv_obj_move_foreground(s_root);

#if !defined(TRAIL_MATE_ESP_BOARD_TAB5)
    if (s_logo)
    {
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, s_logo);
        lv_anim_set_values(&anim, LV_OPA_0, LV_OPA_COVER);
        lv_anim_set_time(&anim, kFadeMs);
        lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)set_logo_opa);
        lv_anim_start(&anim);
    }
#endif

    ui::presentation::boot_splash_layout::LogSpec log_spec{};
    log_spec.width = screen_w - (kBootLogPadX * 2);
    log_spec.inset_x = kBootLogPadX;
    log_spec.bottom_inset = kBootLogPadBottom;
    log_spec.text_color = ::ui::theme::text_muted();
    log_spec.font = &lv_font_montserrat_12;
    s_log_label = ui::presentation::boot_splash_layout::create_log_label(s_root, log_spec);
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
