#pragma once

#include "ui/theme/theme_registry.h"

namespace ui::theme
{

inline void apply_component_profile_to_style(lv_style_t* style, const ComponentProfile& profile)
{
    if (!style)
    {
        return;
    }

    if (profile.bg_opa >= 0)
    {
        lv_style_set_bg_opa(style, static_cast<lv_opa_t>(profile.bg_opa));
    }
    if (profile.bg_color.present)
    {
        lv_style_set_bg_color(style, profile.bg_color.value);
    }
    if (profile.border_width >= 0)
    {
        lv_style_set_border_width(style, profile.border_width);
    }
    if (profile.border_color.present)
    {
        lv_style_set_border_color(style, profile.border_color.value);
    }
    if (profile.radius >= 0)
    {
        lv_style_set_radius(style, profile.radius);
    }
    if (profile.pad_all >= 0)
    {
        lv_style_set_pad_all(style, profile.pad_all);
    }
    if (profile.pad_row >= 0)
    {
        lv_style_set_pad_row(style, profile.pad_row);
    }
    if (profile.pad_column >= 0)
    {
        lv_style_set_pad_column(style, profile.pad_column);
    }
    if (profile.pad_left >= 0)
    {
        lv_style_set_pad_left(style, profile.pad_left);
    }
    if (profile.pad_right >= 0)
    {
        lv_style_set_pad_right(style, profile.pad_right);
    }
    if (profile.pad_top >= 0)
    {
        lv_style_set_pad_top(style, profile.pad_top);
    }
    if (profile.pad_bottom >= 0)
    {
        lv_style_set_pad_bottom(style, profile.pad_bottom);
    }
    if (profile.shadow_width >= 0)
    {
        lv_style_set_shadow_width(style, profile.shadow_width);
    }
    if (profile.shadow_color.present)
    {
        lv_style_set_shadow_color(style, profile.shadow_color.value);
    }
    if (profile.shadow_opa >= 0)
    {
        lv_style_set_shadow_opa(style, static_cast<lv_opa_t>(profile.shadow_opa));
    }
    if (profile.text_color.present)
    {
        lv_style_set_text_color(style, profile.text_color.value);
    }
}

inline void apply_component_profile_to_obj(lv_obj_t* obj,
                                           const ComponentProfile& profile,
                                           lv_style_selector_t selector = 0)
{
    if (!obj)
    {
        return;
    }

    if (profile.bg_opa >= 0)
    {
        lv_obj_set_style_bg_opa(obj, static_cast<lv_opa_t>(profile.bg_opa), selector);
    }
    if (profile.bg_color.present)
    {
        lv_obj_set_style_bg_color(obj, profile.bg_color.value, selector);
    }
    if (profile.border_width >= 0)
    {
        lv_obj_set_style_border_width(obj, profile.border_width, selector);
    }
    if (profile.border_color.present)
    {
        lv_obj_set_style_border_color(obj, profile.border_color.value, selector);
    }
    if (profile.radius >= 0)
    {
        lv_obj_set_style_radius(obj, profile.radius, selector);
    }
    if (profile.pad_all >= 0)
    {
        lv_obj_set_style_pad_all(obj, profile.pad_all, selector);
    }
    if (profile.pad_row >= 0)
    {
        lv_obj_set_style_pad_row(obj, profile.pad_row, selector);
    }
    if (profile.pad_column >= 0)
    {
        lv_obj_set_style_pad_column(obj, profile.pad_column, selector);
        lv_obj_set_style_pad_gap(obj, profile.pad_column, selector);
    }
    if (profile.pad_left >= 0)
    {
        lv_obj_set_style_pad_left(obj, profile.pad_left, selector);
    }
    if (profile.pad_right >= 0)
    {
        lv_obj_set_style_pad_right(obj, profile.pad_right, selector);
    }
    if (profile.pad_top >= 0)
    {
        lv_obj_set_style_pad_top(obj, profile.pad_top, selector);
    }
    if (profile.pad_bottom >= 0)
    {
        lv_obj_set_style_pad_bottom(obj, profile.pad_bottom, selector);
    }
    if (profile.min_height >= 0)
    {
        lv_obj_set_style_min_height(obj, profile.min_height, selector);
    }
    if (profile.shadow_width >= 0)
    {
        lv_obj_set_style_shadow_width(obj, profile.shadow_width, selector);
    }
    if (profile.shadow_color.present)
    {
        lv_obj_set_style_shadow_color(obj, profile.shadow_color.value, selector);
    }
    if (profile.shadow_opa >= 0)
    {
        lv_obj_set_style_shadow_opa(obj, static_cast<lv_opa_t>(profile.shadow_opa), selector);
    }
    if (profile.text_color.present)
    {
        lv_obj_set_style_text_color(obj, profile.text_color.value, selector);
    }
}

} // namespace ui::theme
