#include "ui/widgets/busy_overlay.h"

#include <cstring>

#include "lvgl.h"
#include "platform/ui/screen_runtime.h"
#include "ui/page/page_profile.h"
#include "ui/theme/theme_component_style.h"
#include "ui/ui_theme.h"

#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif

namespace ui::widgets::busy_overlay
{
namespace
{

constexpr lv_coord_t kRequestedWidth = 220;
constexpr lv_coord_t kRequestedHeight = 118;
constexpr lv_coord_t kBarHeight = 8;
constexpr lv_coord_t kBarRadius = 4;
constexpr uint32_t kAnimTimeMs = 820;

lv_obj_t* s_root = nullptr;
lv_obj_t* s_panel = nullptr;
lv_obj_t* s_title_label = nullptr;
lv_obj_t* s_detail_label = nullptr;
lv_obj_t* s_bar = nullptr;
uint32_t s_depth = 0;

void refresh_now()
{
    lv_refr_now(nullptr);
}

void set_bar_value(void* bar, int32_t value)
{
    lv_obj_t* bar_obj = static_cast<lv_obj_t*>(bar);
    if (!bar_obj || !lv_obj_is_valid(bar_obj))
    {
        return;
    }
    lv_bar_set_value(bar_obj, static_cast<int32_t>(value), LV_ANIM_OFF);
}

int clamp_progress(int progress_percent)
{
    if (progress_percent < 0)
    {
        return -1;
    }
    if (progress_percent > 100)
    {
        return 100;
    }
    return progress_percent;
}

void stop_bar_animation()
{
    if (s_bar && lv_obj_is_valid(s_bar))
    {
        lv_anim_del(s_bar, set_bar_value);
    }
}

void start_bar_animation()
{
    if (!s_bar || !lv_obj_is_valid(s_bar))
    {
        return;
    }

    stop_bar_animation();
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 12, LV_ANIM_OFF);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_bar);
    lv_anim_set_values(&anim, 12, 100);
    lv_anim_set_time(&anim, kAnimTimeMs);
    lv_anim_set_playback_time(&anim, kAnimTimeMs);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&anim, set_bar_value);
    lv_anim_start(&anim);
}

void apply_progress(int progress_percent)
{
    if (!s_bar || !lv_obj_is_valid(s_bar))
    {
        return;
    }

    const int clamped = clamp_progress(progress_percent);
    if (clamped < 0)
    {
        start_bar_animation();
        return;
    }

    stop_bar_animation();
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, clamped, LV_ANIM_OFF);
}

void swallow_event_cb(lv_event_t* e)
{
    lv_event_stop_bubbling(e);
    lv_event_stop_processing(e);
    if (lv_indev_t* indev = lv_event_get_indev(e))
    {
        lv_indev_stop_processing(indev);
    }
}

void destroy_overlay()
{
    stop_bar_animation();
    if (s_root && lv_obj_is_valid(s_root))
    {
        lv_obj_del(s_root);
    }
    s_root = nullptr;
    s_panel = nullptr;
    s_title_label = nullptr;
    s_detail_label = nullptr;
    s_bar = nullptr;
}

void ensure_overlay()
{
    if (s_root && lv_obj_is_valid(s_root))
    {
        return;
    }

    lv_obj_t* parent = lv_layer_top();
    if (!parent)
    {
        return;
    }

    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, ::ui::theme::color(::ui::theme::ColorSlot::OverlayScrim), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_root, swallow_event_cb, LV_EVENT_ALL, nullptr);

    const auto size = ::ui::page_profile::resolve_modal_size(kRequestedWidth, kRequestedHeight, s_root);
    const lv_coord_t pad = ::ui::page_profile::resolve_modal_pad();
    ::ui::theme::ComponentProfile panel_profile{};
    ::ui::theme::ComponentProfile bar_profile{};
    (void)::ui::theme::resolve_component_profile(::ui::theme::ComponentSlot::BusyOverlayPanel,
                                                 panel_profile);
    (void)::ui::theme::resolve_component_profile(::ui::theme::ComponentSlot::BusyOverlayProgressBar,
                                                 bar_profile);

    s_panel = lv_obj_create(s_root);
    lv_obj_set_size(s_panel, size.width, size.height);
    lv_obj_center(s_panel);
    lv_obj_set_style_bg_color(s_panel, ::ui::theme::surface(), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_border_color(s_panel, ::ui::theme::border(), 0);
    lv_obj_set_style_radius(s_panel, 10, 0);
    lv_obj_set_style_pad_all(s_panel, pad, 0);
    lv_obj_set_style_pad_row(s_panel, pad, 0);
    lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_panel, swallow_event_cb, LV_EVENT_ALL, nullptr);
    ::ui::theme::apply_component_profile_to_obj(s_panel, panel_profile);

    s_title_label = lv_label_create(s_panel);
    lv_obj_set_width(s_title_label, LV_PCT(100));
    lv_obj_set_style_text_align(s_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_title_label, ::ui::theme::text(), 0);
    lv_label_set_long_mode(s_title_label, LV_LABEL_LONG_WRAP);

    s_detail_label = lv_label_create(s_panel);
    lv_obj_set_width(s_detail_label, LV_PCT(100));
    lv_obj_set_style_text_align(s_detail_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_detail_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_detail_label, ::ui::theme::text_muted(), 0);
    lv_label_set_long_mode(s_detail_label, LV_LABEL_LONG_DOT);

    s_bar = lv_bar_create(s_panel);
    lv_obj_set_width(s_bar, LV_PCT(100));
    lv_obj_set_height(s_bar, kBarHeight);
    lv_obj_set_style_radius(s_bar, kBarRadius, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, ::ui::theme::separator(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, kBarRadius, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bar,
                              bar_profile.accent_color.present ? bar_profile.accent_color.value
                                                               : ::ui::theme::accent(),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    ::ui::theme::apply_component_profile_to_obj(s_bar, bar_profile, LV_PART_MAIN);

    start_bar_animation();
    lv_obj_move_foreground(s_root);
}

void set_label_text(lv_obj_t* label, const char* text)
{
    if (!label || !lv_obj_is_valid(label))
    {
        return;
    }
    lv_label_set_text(label, text ? text : "");
}

void update_content(const char* title, const char* detail)
{
    set_label_text(s_title_label, title ? title : "Loading...");
    set_label_text(s_detail_label, detail ? detail : "");
    if (detail && detail[0] != '\0')
    {
        lv_obj_clear_flag(s_detail_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_detail_label, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace

void show(const char* title, const char* detail)
{
    ++s_depth;
    ::platform::ui::screen::disable_sleep();
    ensure_overlay();
    if (!s_root)
    {
        return;
    }

    update_content(title, detail);
    apply_progress(-1);
    lv_obj_move_foreground(s_root);
    refresh_now();
}

void update(const char* title, const char* detail)
{
    if (!s_root || !lv_obj_is_valid(s_root))
    {
        return;
    }

    update_content(title, detail);
    lv_obj_move_foreground(s_root);
    refresh_now();
}

void set_progress(int progress_percent)
{
    if (!s_root || !lv_obj_is_valid(s_root))
    {
        return;
    }

    apply_progress(progress_percent);
    refresh_now();
}

void hide()
{
    if (s_depth == 0)
    {
        return;
    }

    --s_depth;
    ::platform::ui::screen::enable_sleep();
    if (s_depth > 0)
    {
        return;
    }

    destroy_overlay();
    refresh_now();
}

bool visible()
{
    return s_root != nullptr && lv_obj_is_valid(s_root);
}

} // namespace ui::widgets::busy_overlay
