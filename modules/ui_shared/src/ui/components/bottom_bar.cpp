#include "ui/components/bottom_bar.h"

#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include <algorithm>

namespace ui::components::bottom_bar
{
namespace
{
constexpr uint32_t kBackground = 0xF6E6C6;
constexpr uint32_t kText = 0x6B4A1E;
constexpr uint32_t kSelection = 0xE8C98A;

bool valid(lv_obj_t* object) { return object && lv_obj_is_valid(object); }

void plain(lv_obj_t* object)
{
    lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(object, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}
} // namespace

lv_obj_t* create(lv_obj_t* parent)
{
    auto* bar = lv_obj_create(parent);
    plain(bar);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, LV_PCT(100), kHeight);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kBackground), LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_column(bar, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return bar;
}

void style_item(lv_obj_t* item)
{
    plain(item);
    for (const auto state : {LV_STATE_FOCUSED, LV_STATE_CHECKED, LV_STATE_PRESSED})
    {
        lv_obj_set_style_radius(item, 0, state);
        lv_obj_set_style_border_width(item, 0, state);
        lv_obj_set_style_shadow_width(item, 0, state);
    }
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(item, lv_color_hex(kSelection), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(item, lv_color_hex(kSelection), LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(item, lv_color_hex(kSelection), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(item, lv_color_hex(kText), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(item, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(item, 3, LV_PART_MAIN);
    if (auto* label = lv_obj_get_child(item, 0))
    {
        if (lv_obj_has_class(label, &lv_label_class))
        {
            lv_obj_set_width(label, LV_PCT(100));
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            lv_obj_set_style_text_font(label, ::ui::page_profile::resolve_body_font(), LV_PART_MAIN);
            lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
        }
    }
}

void style_button(lv_obj_t* button)
{
    lv_obj_set_style_bg_color(button, lv_color_hex(0xF8E6C3), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(0x8A6E43), LV_PART_MAIN);
    lv_obj_set_style_radius(button, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(button, lv_color_hex(0x25170D), LV_PART_MAIN);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* add_button(lv_obj_t* bar, const char* text, lv_event_cb_t callback, void* context)
{
    auto* button = lv_btn_create(bar);
    auto* label = lv_label_create(button);
    ::ui::i18n::set_label_text(label, text);
    style_button(button);
    lv_obj_set_style_pad_hor(button, 6, LV_PART_MAIN);
    lv_obj_set_size(button, LV_SIZE_CONTENT, kButtonHeight);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_center(label);
    if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, context);
    return button;
}

void fit_menu(lv_obj_t* panel, lv_coord_t available_width)
{
    lv_coord_t width = 72;
    const auto padding = lv_obj_get_style_pad_left(panel, LV_PART_MAIN) +
                         lv_obj_get_style_pad_right(panel, LV_PART_MAIN) +
                         2 * lv_obj_get_style_border_width(panel, LV_PART_MAIN);
    for (uint32_t index = 0; index < lv_obj_get_child_count(panel); ++index)
    {
        auto* child = lv_obj_get_child(panel, index);
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
        auto* label = lv_obj_has_class(child, &lv_label_class) ? child : lv_obj_get_child(child, 0);
        if (!label || !lv_obj_has_class(label, &lv_label_class)) continue;
        lv_point_t extent{};
        lv_text_get_size(&extent, lv_label_get_text(label), lv_obj_get_style_text_font(label, LV_PART_MAIN),
                         lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        const auto inset = lv_obj_get_style_pad_left(child, LV_PART_MAIN) +
                           lv_obj_get_style_pad_right(child, LV_PART_MAIN) +
                           2 * lv_obj_get_style_border_width(child, LV_PART_MAIN);
        width = std::max<lv_coord_t>(width, extent.x + inset + padding);
    }
    lv_obj_set_width(panel, std::min(width, available_width));
    for (uint32_t index = 0; index < lv_obj_get_child_count(panel); ++index)
        lv_obj_set_width(lv_obj_get_child(panel, index), LV_PCT(100));
}

void Menu::park(lv_obj_t* object)
{
    if (!valid(object) || !valid(root_)) return;
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(object, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_parent(object, root_);
}

void Menu::init(lv_obj_t* root, lv_obj_t* anchor, lv_obj_t* content, Changed changed, void* context, MenuOptions options)
{
    reset();
    root_ = root;
    anchor_ = anchor;
    content_ = content;
    changed_ = changed;
    context_ = context;
    options_ = options;
    park(content_);
    if (valid(anchor_) && options_.anchor_toggles) lv_obj_add_event_cb(anchor_, anchor_event, LV_EVENT_CLICKED, this);
}

void Menu::reset()
{
    changed_ = nullptr;
    close();
    if (valid(anchor_) && options_.anchor_toggles) lv_obj_remove_event_cb(anchor_, anchor_event);
    root_ = anchor_ = content_ = nullptr;
    context_ = nullptr;
}

void Menu::present()
{
    if (!valid(content_) || !valid(layer_)) return;
    lv_obj_set_parent(content_, layer_);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_HIDDEN);
    plain(content_);
    lv_obj_set_flex_grow(content_, 0);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(content_, lv_color_hex(kBackground), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(content_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(content_, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(content_, lv_color_hex(0xD8B27A), LV_PART_MAIN);
    lv_obj_set_style_radius(content_, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content_, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(content_, 2, LV_PART_MAIN);
    lv_obj_set_style_margin_all(content_, 0, LV_PART_MAIN);
    lv_obj_set_height(content_, LV_SIZE_CONTENT);
    // Font and padding must be final before measuring translated item labels.
    for (uint32_t index = 0; index < lv_obj_get_child_count(content_); ++index)
    {
        auto* item = lv_obj_get_child(content_, index);
        if (lv_obj_has_flag(item, LV_OBJ_FLAG_CLICKABLE)) style_item(item);
    }
    lv_obj_update_layout(root_);
    fit_menu(content_, std::max<lv_coord_t>(72, lv_obj_get_width(root_) - 8));
    lv_obj_set_style_max_width(content_, lv_obj_get_width(root_) - 8, LV_PART_MAIN);
    lv_area_t root_area{};
    lv_obj_get_coords(root_, &root_area);
    lv_area_t anchor_area{};
    if (valid(anchor_)) lv_obj_get_coords(anchor_, &anchor_area);
    const lv_coord_t bottom = valid(anchor_) ? anchor_area.y1 - root_area.y1 - 3 : lv_obj_get_height(root_) - 3;
    lv_obj_set_style_max_height(content_, std::max<lv_coord_t>(24, bottom - 4), LV_PART_MAIN);
    lv_obj_add_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_update_layout(content_);
    const auto width = lv_obj_get_width(content_);
    const auto x = valid(anchor_) ? anchor_area.x2 - root_area.x1 - width + 1 : lv_obj_get_width(root_) - width - 4;
    lv_obj_set_pos(content_, std::max<lv_coord_t>(4, std::min<lv_coord_t>(x, lv_obj_get_width(root_) - width - 4)),
                   std::max<lv_coord_t>(4, bottom - lv_obj_get_height(content_)));
    prepare_keys(content_);
}

void Menu::prepare_keys(lv_obj_t* object)
{
    lv_obj_remove_event_cb(object, key_event);
    if (options_.handle_back) lv_obj_add_event_cb(object, key_event, static_cast<lv_event_code_t>(LV_EVENT_KEY | LV_EVENT_PREPROCESS), this);
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i) prepare_keys(lv_obj_get_child(object, i));
}

void Menu::open()
{
    if (is_open() || !valid(root_) || !valid(content_)) return;
    layer_ = lv_obj_create(root_);
    lv_obj_remove_style_all(layer_);
    lv_obj_add_flag(layer_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(layer_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(layer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(layer_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(layer_, 0, 0);
    lv_obj_add_event_cb(layer_, outside_event, LV_EVENT_CLICKED, this);
    present();
    if (changed_) changed_(context_, true);
}

void Menu::close()
{
    if (!layer_) return;
    if (parent_content_)
    {
        if (valid(content_)) lv_obj_del(content_);
        content_ = parent_content_;
        parent_content_ = nullptr;
    }
    park(content_);
    if (valid(layer_)) lv_obj_del(layer_);
    layer_ = nullptr;
    if (changed_) changed_(context_, false);
}

void Menu::toggle() { is_open() ? close() : open(); }

void Menu::show_submenu(lv_obj_t* content)
{
    if (!is_open() || parent_content_ || !valid(content)) return;
    park(content_);
    parent_content_ = content_;
    content_ = content;
    present();
}

bool Menu::back()
{
    if (!is_open()) return false;
    if (!parent_content_) close();
    else
    {
        lv_obj_del(content_);
        content_ = parent_content_;
        parent_content_ = nullptr;
        present();
        if (changed_) changed_(context_, true);
    }
    return true;
}

void Menu::anchor_event(lv_event_t* event)
{
    static_cast<Menu*>(lv_event_get_user_data(event))->toggle();
}

void Menu::outside_event(lv_event_t* event)
{
    if (lv_event_get_target(event) == lv_event_get_current_target(event))
        static_cast<Menu*>(lv_event_get_user_data(event))->close();
}

void Menu::key_event(lv_event_t* event)
{
    const auto key = lv_event_get_key(event);
    if (key == LV_KEY_UP || key == LV_KEY_DOWN)
    {
        auto* target = lv_event_get_target_obj(event);
        auto* group = target ? lv_obj_get_group(target) : nullptr;
        if (!group) return;
        lv_event_stop_processing(event);
        lv_event_stop_bubbling(event);
        if (key == LV_KEY_UP) lv_group_focus_prev(group);
        else lv_group_focus_next(group);
        if (auto* focused = lv_group_get_focused(group))
            lv_obj_scroll_to_view(focused, LV_ANIM_OFF);
        return;
    }
    if (key != LV_KEY_ESC && key != LV_KEY_BACKSPACE && key != LV_KEY_LEFT) return;
    auto* menu = static_cast<Menu*>(lv_event_get_user_data(event));
    lv_event_stop_processing(event);
    lv_event_stop_bubbling(event);
    menu->back();
}
} // namespace ui::components::bottom_bar
