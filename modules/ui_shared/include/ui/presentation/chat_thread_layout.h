#pragma once

#include "lvgl.h"

#include <cstdint>

namespace ui::presentation::chat_thread_layout
{

struct RootSpec
{
    int pad_row = 0;
};

struct ThreadSpec
{
    int pad_row = 0;
    lv_scrollbar_mode_t scrollbar_mode = LV_SCROLLBAR_MODE_OFF;
};

struct ActionBarSpec
{
    lv_coord_t height = 30;
    int pad_column = 0;
    lv_flex_align_t main_align = LV_FLEX_ALIGN_CENTER;
};

struct ActionButtonSpec
{
    lv_coord_t width = 120;
    lv_coord_t height = 28;
};

enum class ActionButtonRole : std::uint8_t
{
    Primary = 0,
    Secondary,
};

void make_non_scrollable(lv_obj_t* obj);
lv_obj_t* create_root(lv_obj_t* parent, const RootSpec& spec = {});
lv_obj_t* create_thread_region(lv_obj_t* parent, const ThreadSpec& spec = {});
lv_obj_t* create_action_bar(lv_obj_t* parent, const ActionBarSpec& spec = {});
lv_obj_t* create_action_button(lv_obj_t* parent,
                               lv_obj_t*& out_label,
                               ActionButtonRole role = ActionButtonRole::Primary,
                               const ActionButtonSpec& spec = {});
lv_obj_t* create_thread_row(lv_obj_t* parent);
lv_obj_t* create_bubble(lv_obj_t* parent);
lv_obj_t* create_bubble_text(lv_obj_t* parent);
lv_obj_t* create_bubble_time(lv_obj_t* parent);
lv_obj_t* create_bubble_status(lv_obj_t* parent);
void align_thread_row(lv_obj_t* row, bool is_self);
void set_bubble_max_width(lv_obj_t* bubble, lv_coord_t max_w);
lv_coord_t thread_content_width(lv_obj_t* thread_region);

} // namespace ui::presentation::chat_thread_layout
