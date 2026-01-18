/**
 * UI Wireframe / Layout Tree (Correct: 3 columns in one ROW)
 * --------------------------------------------------------------------
 *
 * Root Container (ROW, full screen)
 *
 * ┌──────────────────────────────────────────────────────────────────┐
 * │ ┌──────────────┐ ┌───────────────────────────────┐ ┌──────────────┐ │
 * │ │ Filter Panel │ │           List Panel          │ │ Action Panel │ │
 * │ │   (80px)     │ │        (flex-grow = 1)        │ │   (80px)     │ │
 * │ │ ┌──────────┐ │ │ ┌───────────────────────────┐ │ │           │ │
 * │ │ │ Contacts │ │ │ │ List Container            │ │ │ (context  │ │
 * │ │ └──────────┘ │ │ │ (COLUMN, flex-grow = 1)   │ │ │ actions)  │ │
 * │ │ ┌──────────┐ │ │ │                           │ │ │           │ │
 * │ │ │ Nearby   │ │ │ │  List Item × 4 / page     │ │ │           │ │
 * │ │ └──────────┘ │ │ │  [Name ..........] [St.]  │ │ │           │ │
 * │ │              │ │ │  ...                      │ │ │           │ │
 * │ │              │ │ └───────────────────────────┘ │ │           │ │
 * │ │              │ │ ┌───────────────────────────┐ │ │           │ │
 * │ │              │ │ │ Bottom Bar (ROW)          │ │ │           │ │
 * │ │              │ │ │ Prev | Next | Back        │ │ │           │ │
 * │ │              │ │ └───────────────────────────┘ │ │           │ │
 * │ └──────────────┘ └───────────────────────────────┘ └───────────┘ │
 * └──────────────────────────────────────────────────────────────────┘
 *
 * Tree view:
 * Root(ROW)
 * ├─ FilterPanel(80,COL) -> ContactsBtn, NearbyBtn
 * ├─ ListPanel(grow=1,COL)
 * │   ├─ ListContainer(grow=1,COL) -> ListItem(x4/page)-> NameLabel, StatusLabel
 * │   └─ BottomBar(ROW) -> PrevBtn, NextBtn, BackBtn
 * └─ ActionPanel(80,COL) -> (TBD action buttons)
 * 
 * 
  * Preconditions:
 * - The parent/root container uses LV_FLEX_FLOW_ROW to place 3 panels horizontally.
 *
 * Implementation notes:
 * - ListPanel contains two children: sub_container (flex_grow=1) and bottom_container (ROW).
 * - ActionPanel is a placeholder; action buttons are created elsewhere.
 */

/**
 * @file contacts_page_layout.cpp
 * @brief Contacts layout
 */

#include <Arduino.h>
#include "contacts_page_layout.h"

using namespace contacts::ui;

namespace contacts {
namespace ui {
namespace layout {

// 布局常量
static constexpr int kFilterPanelWidth = 80;
static constexpr int kActionPanelWidth = 80;
static constexpr int kButtonHeight = 32;
static constexpr int kButtonSpacing = 3;
static constexpr int kPanelGap = 3;           // 三列之间的间距
static constexpr int kScreenEdgePadding = 3;   // 屏幕边缘的padding
static constexpr int kTopBarContentGap = 3;    // TopBar与Content之间的间距

// 工具函数
static void make_non_scrollable(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void apply_base_container_style(lv_obj_t* obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    make_non_scrollable(obj);
}

lv_obj_t* create_root(lv_obj_t* parent)
{
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);

    // 样式：背景透明，让子元素控制自己的背景色
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    apply_base_container_style(root);

    // TopBar与Content之间的间距
    lv_obj_set_style_pad_row(root, kTopBarContentGap, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    return root;
}

lv_obj_t* create_header(lv_obj_t* root,
                        void (*back_callback)(void*),
                        void* user_data)
{
    lv_obj_t* header = lv_obj_create(root);
    lv_obj_set_size(header, LV_PCT(100), ::ui::widgets::kTopBarHeight);

    // 样式
    lv_obj_set_style_bg_color(header, lv_color_white(), 0);
    apply_base_container_style(header);
    lv_obj_set_style_pad_all(header, 0, 0);

    // 初始化 TopBar
    ::ui::widgets::TopBarConfig cfg;
    cfg.height = ::ui::widgets::kTopBarHeight;
    ::ui::widgets::top_bar_init(g_contacts_state.top_bar, header, cfg);
    ::ui::widgets::top_bar_set_title(g_contacts_state.top_bar, "Contacts");
    ::ui::widgets::top_bar_set_back_callback(g_contacts_state.top_bar, back_callback, user_data);

    return header;
}

lv_obj_t* create_content(lv_obj_t* root)
{
    lv_obj_t* content = lv_obj_create(root);

    // 尺寸和flex
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_height(content, 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content,
                         LV_FLEX_ALIGN_START,
                         LV_FLEX_ALIGN_START,
                         LV_FLEX_ALIGN_START);

    // 样式
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    apply_base_container_style(content);

    // ✅ 统一间距控制：只设置屏幕边缘的padding
    // 列之间的间距由panel的margin控制
    lv_obj_set_style_pad_left(content, kScreenEdgePadding, 0);
    lv_obj_set_style_pad_right(content, kScreenEdgePadding, 0);
    lv_obj_set_style_pad_top(content, 0, 0);
    lv_obj_set_style_pad_bottom(content, 0, 0);

    return content;
}

void create_filter_panel(lv_obj_t* parent)
{
    g_contacts_state.filter_panel = lv_obj_create(parent);
    make_non_scrollable(g_contacts_state.filter_panel);

    // 先应用风格（避免它覆盖我们后面尺寸/边距）
    style::apply_panel_side(g_contacts_state.filter_panel);

    // ✅ 固定宽度
    lv_obj_set_width(g_contacts_state.filter_panel, kFilterPanelWidth);
    lv_obj_set_height(g_contacts_state.filter_panel, LV_PCT(100));
    lv_obj_set_flex_flow(g_contacts_state.filter_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_contacts_state.filter_panel, 3, LV_PART_MAIN);

    // ✅ 统一间距：使用margin，移除负margin
    // Filter是左边缘第一个，所以margin_left=0（屏幕边缘padding由content控制）
    // Filter右侧需要与List保持间距
    lv_obj_set_style_margin_left(g_contacts_state.filter_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_right(g_contacts_state.filter_panel, kPanelGap, LV_PART_MAIN);

    // Buttons
    g_contacts_state.contacts_btn = lv_btn_create(g_contacts_state.filter_panel);
    make_non_scrollable(g_contacts_state.contacts_btn);
    style::apply_btn_filter(g_contacts_state.contacts_btn);
    lv_obj_set_size(g_contacts_state.contacts_btn, LV_PCT(100), kButtonHeight);
    lv_obj_t* contacts_label = lv_label_create(g_contacts_state.contacts_btn);
    lv_label_set_text(contacts_label, "Contacts");
    lv_obj_center(contacts_label);

    g_contacts_state.nearby_btn = lv_btn_create(g_contacts_state.filter_panel);
    make_non_scrollable(g_contacts_state.nearby_btn);
    style::apply_btn_filter(g_contacts_state.nearby_btn);
    lv_obj_set_size(g_contacts_state.nearby_btn, LV_PCT(100), kButtonHeight);
    lv_obj_t* nearby_label = lv_label_create(g_contacts_state.nearby_btn);
    lv_label_set_text(nearby_label, "Nearby");
    lv_obj_center(nearby_label);
}

void create_list_panel(lv_obj_t* parent)
{
    g_contacts_state.list_panel = lv_obj_create(parent);
    make_non_scrollable(g_contacts_state.list_panel);

    // 先 apply，避免覆盖 grow/width
    style::apply_panel_main(g_contacts_state.list_panel);

    // ✅ ROW flex 中间列：必须 width=0 + flex_grow=1 才能“吃掉剩余空间”
    lv_obj_set_height(g_contacts_state.list_panel, LV_PCT(100));
    lv_obj_set_width(g_contacts_state.list_panel, 0);
    lv_obj_set_flex_grow(g_contacts_state.list_panel, 1);

    lv_obj_set_flex_flow(g_contacts_state.list_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_contacts_state.list_panel, 2, LV_PART_MAIN);

    // ✅ 统一间距：中间列不需要margin，两侧的间距由filter/action控制
    lv_obj_set_style_margin_left(g_contacts_state.list_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_right(g_contacts_state.list_panel, 0, LV_PART_MAIN);
}

void create_action_panel(lv_obj_t* parent)
{
    g_contacts_state.action_panel = lv_obj_create(parent);
    make_non_scrollable(g_contacts_state.action_panel);

    // 先 apply，避免覆盖尺寸
    style::apply_panel_side(g_contacts_state.action_panel);

    // ✅ 固定宽度
    lv_obj_set_width(g_contacts_state.action_panel, kActionPanelWidth);
    lv_obj_set_height(g_contacts_state.action_panel, LV_PCT(100));
    lv_obj_set_flex_flow(g_contacts_state.action_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_contacts_state.action_panel, kButtonSpacing, LV_PART_MAIN);

    // ✅ 统一间距：Action左侧需要与List保持间距
    // Action右侧margin=0（屏幕边缘padding由content控制）
    lv_obj_set_style_margin_left(g_contacts_state.action_panel, kPanelGap, LV_PART_MAIN);
    lv_obj_set_style_margin_right(g_contacts_state.action_panel, 0, LV_PART_MAIN);
}

void ensure_list_subcontainers()
{
    if (g_contacts_state.list_panel == nullptr) return;

    if (g_contacts_state.sub_container == nullptr) {
        g_contacts_state.sub_container = lv_obj_create(g_contacts_state.list_panel);
        make_non_scrollable(g_contacts_state.sub_container);

        style::apply_container_white(g_contacts_state.sub_container);

        // ✅ sub_container 吃掉剩余高度，bottom_container 才能在下方
        lv_obj_set_width(g_contacts_state.sub_container, LV_PCT(100));
        lv_obj_set_height(g_contacts_state.sub_container, 0);
        lv_obj_set_flex_grow(g_contacts_state.sub_container, 1);

        lv_obj_set_flex_flow(g_contacts_state.sub_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(g_contacts_state.sub_container, 2, LV_PART_MAIN);
    }

    if (g_contacts_state.bottom_container == nullptr) {
        g_contacts_state.bottom_container = lv_obj_create(g_contacts_state.list_panel);
        make_non_scrollable(g_contacts_state.bottom_container);

        style::apply_container_white(g_contacts_state.bottom_container);

        lv_obj_set_size(g_contacts_state.bottom_container, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(g_contacts_state.bottom_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(g_contacts_state.bottom_container, 2, LV_PART_MAIN);
        lv_obj_set_flex_align(g_contacts_state.bottom_container,
                              LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
    }
}

lv_obj_t* create_list_item(lv_obj_t* parent,
                           const chat::contacts::NodeInfo& node,
                           ContactsMode /*mode*/,
                           const char* status_text)
{
    lv_obj_t* item = lv_obj_create(parent);
    lv_obj_set_size(item, LV_PCT(100), 32);

    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    make_non_scrollable(item);

    style::apply_list_item(item);

    lv_obj_t* name_label = lv_label_create(item);
    lv_label_set_text(name_label, node.display_name.c_str());
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 10, 0);
    style::apply_label_primary(name_label);

    lv_obj_t* status_label = lv_label_create(item);
    lv_label_set_text(status_label, status_text);
    lv_obj_align(status_label, LV_ALIGN_RIGHT_MID, -10, 0);
    style::apply_label_muted(status_label);

    g_contacts_state.list_items.push_back(item);
    return item;
}

} // namespace layout
} // namespace ui
} // namespace contacts

