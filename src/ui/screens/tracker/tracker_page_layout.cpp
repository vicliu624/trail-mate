/**
 * UI Wireframe / Layout Tree
 * ------------------------------------------------------------
 * Root(COL)
 * |-- Header(TopBar host, fixed height)
 * `-- Content(COL, grow=1)
 *     |-- StartStopBtn(fixed height)
 *     `-- TrackList(list, grow=1)
 */

#include "tracker_page_layout.h"
#include "tracker_state.h"
#include "../../widgets/top_bar.h"

namespace tracker
{
namespace ui
{
namespace layout
{

namespace
{
inline void make_non_scrollable(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}
} // namespace

lv_obj_t* create_root(lv_obj_t* parent)
{
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_pad_row(root, 4, 0);
    make_non_scrollable(root);
    return root;
}

lv_obj_t* create_header(lv_obj_t* root)
{
    lv_obj_t* header = lv_obj_create(root);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, ::ui::widgets::kTopBarHeight);
    lv_obj_set_style_pad_all(header, 0, 0);
    make_non_scrollable(header);
    return header;
}

lv_obj_t* create_content(lv_obj_t* root)
{
    lv_obj_t* content = lv_obj_create(root);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, 8, 0);
    lv_obj_set_style_pad_row(content, 6, 0);
    return content;
}

lv_obj_t* create_start_stop_button(lv_obj_t* content)
{
    lv_obj_t* btn = lv_btn_create(content);
    lv_obj_set_width(btn, LV_PCT(100));
    return btn;
}

lv_obj_t* create_list(lv_obj_t* content)
{
    lv_obj_t* list = lv_list_create(content);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    return list;
}

} // namespace layout
} // namespace ui
} // namespace tracker

