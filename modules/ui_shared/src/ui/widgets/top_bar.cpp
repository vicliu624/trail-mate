/**
 * @file top_bar.cpp
 * @brief Shared top bar widget (bright header reused across pages)
 */

#include "ui/widgets/top_bar.h"

#include <algorithm>
#include <cstring>

#include "ui/assets/fonts/font_utils.h"
#include "ui/page/page_profile.h"
#include "ui/ui_theme.h"

#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_18) || !LV_FONT_MONTSERRAT_18
#define lv_font_montserrat_18 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_20) || !LV_FONT_MONTSERRAT_20
#define lv_font_montserrat_20 lv_font_montserrat_18
#endif

namespace ui
{
namespace widgets
{

static void back_event_cb(lv_event_t* e)
{
    TopBar* bar = static_cast<TopBar*>(lv_event_get_user_data(e));
    if (bar != nullptr && bar->back_cb != nullptr)
    {
        bar->back_cb(bar->back_user_data);
    }
}

static lv_coord_t resolve_top_bar_height(const TopBarConfig& config)
{
    if (config.height > 0)
    {
        return config.height;
    }

    const lv_coord_t profile_height = ::ui::page_profile::current().top_bar_height;
    return profile_height > 0 ? profile_height : static_cast<lv_coord_t>(kTopBarHeight);
}

static const lv_font_t* resolve_top_bar_font(lv_coord_t height)
{
    if (height >= 60)
    {
        return &lv_font_montserrat_20;
    }
    if (height >= 48)
    {
        return &lv_font_montserrat_18;
    }
    if (height >= 40)
    {
        return &lv_font_montserrat_16;
    }
    return &lv_font_montserrat_14;
}

void top_bar_init(TopBar& bar, lv_obj_t* parent, const TopBarConfig& config)
{
    const auto& profile = ::ui::page_profile::current();
    const lv_coord_t resolved_height = resolve_top_bar_height(config);
    const bool large_touch = profile.large_touch_hitbox || resolved_height >= 56;
    const lv_coord_t side_pad = large_touch ? 18 : (resolved_height >= 40 ? 14 : 10);
    const lv_coord_t vertical_pad = large_touch ? 10 : (resolved_height >= 40 ? 8 : 6);
    const lv_coord_t back_btn_height = std::max<lv_coord_t>(large_touch ? 44 : 20,
                                                            resolved_height - (vertical_pad * 2));
    const lv_coord_t back_btn_width = std::max<lv_coord_t>(large_touch ? 68 : 44,
                                                           back_btn_height + (large_touch ? 24 : (resolved_height >= 40 ? 16 : 10)));
    const lv_coord_t back_btn_radius = std::max<lv_coord_t>(large_touch ? 16 : 10, back_btn_height / 2);
    const lv_coord_t right_label_width = large_touch ? 156 : (resolved_height >= 40 ? 120 : 90);
    const lv_font_t* text_font = resolve_top_bar_font(resolved_height);

    bar.container = lv_obj_create(parent);
    lv_obj_set_size(bar.container, LV_PCT(100), resolved_height);
    lv_obj_set_style_bg_color(bar.container, ui::theme::accent(), 0);
    lv_obj_set_style_bg_opa(bar.container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar.container, 0, 0);
    lv_obj_set_style_pad_left(bar.container, side_pad, 0);
    lv_obj_set_style_pad_right(bar.container, side_pad, 0);
    lv_obj_set_style_pad_top(bar.container, vertical_pad, 0);
    lv_obj_set_style_pad_bottom(bar.container, vertical_pad, 0);
    lv_obj_set_style_radius(bar.container, 0, 0);
    lv_obj_clear_flag(bar.container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(bar.container, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_flex_flow(bar.container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar.container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar.container, large_touch ? 12 : 6, 0);

    if (config.back_btn_override != nullptr)
    {
        bar.back_btn = config.back_btn_override;
    }
    else if (config.create_back)
    {
        bar.back_btn = lv_btn_create(bar.container);
        lv_obj_set_size(bar.back_btn, back_btn_width, back_btn_height);
        lv_obj_set_style_bg_color(bar.back_btn, ui::theme::surface(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar.back_btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(bar.back_btn, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(bar.back_btn, ui::theme::border(), LV_PART_MAIN);
        lv_obj_set_style_radius(bar.back_btn, back_btn_radius, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar.back_btn, ui::theme::accent(), LV_STATE_FOCUSED);
        lv_obj_set_style_outline_width(bar.back_btn, 0, LV_STATE_FOCUSED);
        lv_obj_align(bar.back_btn, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_event_cb(bar.back_btn, back_event_cb, LV_EVENT_CLICKED, &bar);
        lv_obj_t* back_label = lv_label_create(bar.back_btn);
        lv_label_set_text(back_label, LV_SYMBOL_LEFT);
        lv_obj_center(back_label);
        lv_obj_set_style_text_color(back_label, ui::theme::text(), 0);
        lv_obj_set_style_text_font(back_label, text_font, 0);
    }

    if (config.title_label_override != nullptr)
    {
        bar.title_label = config.title_label_override;
    }
    else
    {
        bar.title_label = lv_label_create(bar.container);
        lv_label_set_text(bar.title_label, "");
        lv_label_set_long_mode(bar.title_label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(bar.title_label, ui::theme::text(), 0);
        lv_obj_set_style_bg_opa(bar.title_label, LV_OPA_TRANSP, 0);
    }
    lv_obj_set_style_text_font(bar.title_label, text_font, 0);
    lv_obj_set_flex_grow(bar.title_label, 1);
    lv_obj_set_width(bar.title_label, LV_PCT(100));
    lv_obj_set_style_text_align(bar.title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_max_width(bar.title_label, LV_PCT(100), 0);

    bar.right_label = lv_label_create(bar.container);
    lv_label_set_text(bar.right_label, "");
    lv_obj_set_width(bar.right_label, right_label_width);
    lv_label_set_long_mode(bar.right_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(bar.right_label, ui::theme::text_muted(), 0);
    lv_obj_set_style_text_align(bar.right_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(bar.right_label, text_font, 0);
    lv_obj_set_style_bg_opa(bar.right_label, LV_OPA_TRANSP, 0);
}

void top_bar_set_title(TopBar& bar, const char* title)
{
    if (bar.title_label == nullptr || title == nullptr)
    {
        return;
    }

    const char* current = lv_label_get_text(bar.title_label);
    if (current != nullptr && std::strcmp(current, title) == 0)
    {
        return;
    }

    lv_label_set_text(bar.title_label, title);
    ::ui::fonts::apply_localized_font(
        bar.title_label, title, resolve_top_bar_font(lv_obj_get_height(bar.container)));
}

void top_bar_set_right_text(TopBar& bar, const char* text)
{
    if (bar.right_label == nullptr || text == nullptr)
    {
        return;
    }

    const char* current = lv_label_get_text(bar.right_label);
    if (current != nullptr && std::strcmp(current, text) == 0)
    {
        return;
    }

    lv_label_set_text(bar.right_label, text);
    ::ui::fonts::apply_localized_font(
        bar.right_label, text, resolve_top_bar_font(lv_obj_get_height(bar.container)));
}

void top_bar_set_back_callback(TopBar& bar, void (*cb)(void*), void* user_data)
{
    bar.back_cb = cb;
    bar.back_user_data = user_data;
}

} // namespace widgets
} // namespace ui
