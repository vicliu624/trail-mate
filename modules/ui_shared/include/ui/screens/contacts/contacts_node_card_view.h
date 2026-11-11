#pragma once

#include "lvgl.h"

namespace contacts::ui::card_view
{
struct View
{
    lv_obj_t* root = nullptr;
    lv_obj_t* name = nullptr;
    lv_obj_t* identity = nullptr;
    lv_obj_t* region = nullptr;
    lv_obj_t* distance = nullptr;
    lv_obj_t* status = nullptr;
    lv_obj_t* counter = nullptr;
    lv_obj_t* previous = nullptr;
    lv_obj_t* next = nullptr;
    lv_obj_t* details = nullptr;
};

inline View create(lv_obj_t* parent, const lv_font_t* body, const lv_font_t* caption,
                   const lv_font_t* heading, lv_obj_t* action_bar = nullptr)
{
    View view{};
    view.root = lv_obj_create(parent);
    lv_obj_set_width(view.root, LV_PCT(100));
    lv_obj_set_height(view.root, 0);
    lv_obj_set_flex_grow(view.root, 1);
    lv_obj_clear_flag(view.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(view.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(view.root, lv_color_hex(0xFFFCF5), 0);
    lv_obj_set_style_border_color(view.root, lv_color_hex(0xE7C98F), 0);
    lv_obj_set_style_border_width(view.root, 1, 0);
    lv_obj_set_style_radius(view.root, 10, 0);
    lv_obj_set_style_pad_all(view.root, 3, 0);
    lv_obj_set_style_pad_row(view.root, 2, 0);
    lv_obj_set_flex_flow(view.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(view.root, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    auto label = [&](lv_obj_t* target, bool muted)
    {
        auto* result = lv_label_create(target);
        lv_obj_set_width(result, LV_PCT(100));
        lv_label_set_long_mode(result, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(result, lv_color_hex(muted ? 0x8A6A3A : 0x6B4A1E), 0);
        lv_obj_set_style_text_font(result, muted ? caption : body, 0);
        lv_obj_set_height(result, lv_font_get_line_height(muted ? caption : body));
        return result;
    };
    // Backgrounds encode information categories, independent of protocol.
    // Only these fixed containers are added; peer changes reuse every object.
    auto section = [&](uint32_t background)
    {
        auto* result = lv_obj_create(view.root);
        lv_obj_set_width(result, LV_PCT(100));
        lv_obj_clear_flag(result, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(result, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(result, lv_color_hex(background), 0);
        lv_obj_set_style_border_width(result, 0, 0);
        lv_obj_set_style_radius(result, 6, 0);
        lv_obj_set_style_pad_all(result, 3, 0);
        lv_obj_set_style_pad_row(result, 2, 0);
        lv_obj_set_flex_flow(result, LV_FLEX_FLOW_COLUMN);
        return result;
    };
    auto* identity_section = section(0xF6E6C8);
    lv_obj_set_height(identity_section, lv_font_get_line_height(body) + 6);
    lv_obj_set_flex_flow(identity_section, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(identity_section, 4, 0);
    lv_obj_set_flex_align(identity_section, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    view.name = label(identity_section, false);
    lv_obj_set_width(view.name, 0);
    lv_obj_set_flex_grow(view.name, 1);
    view.identity = label(identity_section, true);
    lv_obj_set_width(view.identity, LV_PCT(38));
    lv_obj_set_style_text_align(view.identity, LV_TEXT_ALIGN_RIGHT, 0);
    auto* location_section = section(0xE4EFEB);
    lv_obj_set_height(location_section, 0);
    lv_obj_set_flex_grow(location_section, 1);
    lv_obj_set_flex_align(location_section, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    view.distance = label(location_section, false);
    lv_obj_set_style_text_font(view.distance, heading, 0);
    lv_obj_set_height(view.distance, lv_font_get_line_height(heading));
    lv_obj_set_style_text_color(view.distance, lv_color_hex(0x245B50), 0);
    view.region = label(location_section, false);
    lv_label_set_long_mode(view.region, LV_LABEL_LONG_SCROLL);
    lv_obj_set_style_text_color(view.region, lv_color_hex(0x416F65), 0);
    view.status = label(view.root, true);
    lv_obj_set_style_bg_color(view.status, lv_color_hex(0xEEEDE7), 0);
    lv_obj_set_style_bg_opa(view.status, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(view.status, 4, 0);
    lv_obj_set_style_pad_all(view.status, 2, 0);
    lv_obj_set_style_text_color(view.status, lv_color_hex(0x66675F), 0);
    lv_obj_set_height(view.status, lv_font_get_line_height(caption) + 4);
    auto* controls = lv_obj_create(view.root);
    lv_obj_set_width(controls, LV_PCT(100));
    lv_obj_set_height(controls, action_bar ? LV_SIZE_CONTENT : 26);
    lv_obj_clear_flag(controls, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(controls, 0, 0);
    lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(controls, 0, 0);
    lv_obj_set_style_pad_column(controls, 3, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    auto button = [&](int width, const char* text)
    {
        auto* result = lv_btn_create(action_bar ? action_bar : controls);
        lv_obj_set_size(result, width, 24);
        lv_obj_set_style_bg_color(result, lv_color_hex(0xFAF0D8), 0);
        lv_obj_set_style_border_color(result, lv_color_hex(0xE7C98F), 0);
        lv_obj_set_style_border_width(result, 1, 0);
        lv_obj_set_style_radius(result, 8, 0);
        lv_obj_set_style_pad_all(result, 0, 0);
        auto* text_label = lv_label_create(result);
        lv_obj_set_style_text_font(text_label, caption, 0);
        lv_obj_set_style_text_color(text_label, lv_color_hex(0x6B4A1E), 0);
        lv_label_set_text(text_label, text);
        lv_obj_center(text_label);
        return result;
    };
    view.previous = button(30, LV_SYMBOL_LEFT);
    view.counter = label(controls, true);
    lv_obj_set_width(view.counter, 0);
    lv_obj_set_flex_grow(view.counter, 1);
    lv_obj_set_style_text_align(view.counter, LV_TEXT_ALIGN_CENTER, 0);
    view.next = button(30, LV_SYMBOL_RIGHT);
    view.details = button(48, "Info");
    lv_obj_set_style_bg_color(view.details, lv_color_hex(0xEBA341), 0);
    return view;
}
} // namespace contacts::ui::card_view
