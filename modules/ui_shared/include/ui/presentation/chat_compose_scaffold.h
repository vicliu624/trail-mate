#pragma once

#include "lvgl.h"

#include <cstdint>

namespace ui::presentation::chat_compose_scaffold
{

struct RootSpec
{
    int pad_row = 0;
};

struct HeaderSpec
{
    lv_coord_t height = 0;
};

struct ContentSpec
{
    int pad_all = 0;
    int pad_row = 0;
};

struct EditorSpec
{
    bool grow = true;
};

struct ActionBarSpec
{
    lv_coord_t height = LV_SIZE_CONTENT;
    int pad_left = 0;
    int pad_right = 0;
    int pad_top = 0;
    int pad_bottom = 0;
    int pad_row = 0;
    int pad_column = 0;
    lv_flex_align_t main_align = LV_FLEX_ALIGN_START;
};

struct ActionButtonSpec
{
    lv_coord_t width = 70;
    lv_coord_t height = 28;
};

enum class ActionButtonRole : std::uint8_t
{
    Primary = 0,
    Secondary,
};

void make_non_scrollable(lv_obj_t* obj);
lv_obj_t* create_root(lv_obj_t* parent, const RootSpec& spec = {});
lv_obj_t* create_header_container(lv_obj_t* parent, const HeaderSpec& spec = {});
lv_obj_t* create_content_region(lv_obj_t* parent, const ContentSpec& spec = {});
lv_obj_t* create_editor(lv_obj_t* parent, const EditorSpec& spec = {});
lv_obj_t* create_action_bar(lv_obj_t* parent, const ActionBarSpec& spec = {});
lv_obj_t* create_action_button(lv_obj_t* parent,
                               lv_obj_t*& out_label,
                               ActionButtonRole role = ActionButtonRole::Primary,
                               const ActionButtonSpec& spec = {});
lv_obj_t* create_flex_spacer(lv_obj_t* parent);

} // namespace ui::presentation::chat_compose_scaffold
