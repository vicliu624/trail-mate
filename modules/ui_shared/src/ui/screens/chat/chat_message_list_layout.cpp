#if !defined(ARDUINO_T_WATCH_S3)
/**
 * @file chat_message_list_layout.cpp
 * @brief Layout (structure only) for ChatMessageListScreen
 */

#include "ui/screens/chat/chat_message_list_layout.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/components/two_pane_layout.h"
#include "ui/page/page_profile.h"

namespace chat::ui::layout
{
namespace
{

constexpr int kPanelGap = 0;

struct Metrics
{
    int filter_panel_width = 80;
    int filter_button_height = 28;
    int list_item_height = 28;
    int name_x = 10;
    int preview_x = 120;
    int preview_width = 130;
    int unread_x = 42;
    int time_x = 10;
};

Metrics current_metrics()
{
    const auto& profile = ::ui::page_profile::current();
    Metrics metrics{};
    metrics.filter_panel_width = profile.filter_panel_width;
    metrics.filter_button_height = profile.filter_button_height;
    metrics.list_item_height = profile.list_item_height;
    if (profile.large_touch_hitbox)
    {
        metrics.name_x = 16;
        metrics.preview_x = 170;
        metrics.preview_width = 280;
        metrics.unread_x = 72;
        metrics.time_x = 16;
    }
    else
    {
        metrics.filter_panel_width = std::max(metrics.filter_panel_width, 104);
    }
    return metrics;
}

void style_filter_label(lv_obj_t* label)
{
    if (!label)
    {
        return;
    }

    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    ::ui::fonts::apply_ui_chrome_font(label);
    lv_obj_set_style_text_color(label, lv_color_hex(0x3A2A1A), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
}

lv_obj_t* create_root(lv_obj_t* parent)
{
    return ::ui::components::two_pane_layout::create_root(parent);
}

lv_obj_t* create_content(lv_obj_t* parent)
{
    return ::ui::components::two_pane_layout::create_content_row(parent);
}

lv_obj_t* create_filter_panel(lv_obj_t* parent,
                              lv_obj_t** direct_btn,
                              lv_obj_t** broadcast_btn,
                              lv_obj_t** team_btn)
{
    const auto& profile = ::ui::page_profile::current();
    const Metrics metrics = current_metrics();

    ::ui::components::two_pane_layout::SidePanelSpec panel_spec;
    panel_spec.width = metrics.filter_panel_width;
    panel_spec.pad_row = profile.filter_panel_pad_row;
    panel_spec.margin_left = 0;
    panel_spec.margin_right = profile.large_touch_hitbox ? 8 : kPanelGap;
    lv_obj_t* panel = ::ui::components::two_pane_layout::create_side_panel(parent, panel_spec);

    lv_obj_t* direct = lv_btn_create(panel);
    lv_obj_set_size(direct, LV_PCT(100), metrics.filter_button_height);
    ::ui::components::two_pane_layout::make_non_scrollable(direct);
    lv_obj_t* direct_label = lv_label_create(direct);
    lv_label_set_text(direct_label, "Direct");
    style_filter_label(direct_label);
    lv_obj_center(direct_label);

    lv_obj_t* broadcast = lv_btn_create(panel);
    lv_obj_set_size(broadcast, LV_PCT(100), metrics.filter_button_height);
    ::ui::components::two_pane_layout::make_non_scrollable(broadcast);
    lv_obj_t* broadcast_label = lv_label_create(broadcast);
    lv_label_set_text(broadcast_label, "Broadcast");
    style_filter_label(broadcast_label);
    lv_obj_center(broadcast_label);

    lv_obj_t* team = lv_btn_create(panel);
    lv_obj_set_size(team, LV_PCT(100), metrics.filter_button_height);
    ::ui::components::two_pane_layout::make_non_scrollable(team);
    lv_obj_t* team_label = lv_label_create(team);
    lv_label_set_text(team_label, "Team");
    style_filter_label(team_label);
    lv_obj_center(team_label);
    lv_obj_add_flag(team, LV_OBJ_FLAG_HIDDEN);

    if (direct_btn) *direct_btn = direct;
    if (broadcast_btn) *broadcast_btn = broadcast;
    if (team_btn) *team_btn = team;
    return panel;
}

lv_obj_t* create_list_panel(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();

    ::ui::components::two_pane_layout::MainPanelSpec panel_spec;
    panel_spec.pad_all = profile.large_touch_hitbox ? 6 : 3;
    panel_spec.pad_row = profile.list_panel_pad_row;
    panel_spec.pad_left = profile.list_panel_pad_left;
    panel_spec.pad_right = profile.list_panel_pad_right;
    panel_spec.scrollbar_mode = LV_SCROLLBAR_MODE_OFF;
    return ::ui::components::two_pane_layout::create_main_panel(parent, panel_spec);
}

} // namespace

MessageListLayout create_layout(lv_obj_t* parent)
{
    MessageListLayout w{};
    w.root = create_root(parent);
    w.content = create_content(w.root);
    w.filter_panel = create_filter_panel(w.content, &w.direct_btn, &w.broadcast_btn, &w.team_btn);
    w.list_panel = create_list_panel(w.content);
    return w;
}

MessageItemWidgets create_message_item(lv_obj_t* parent)
{
    const Metrics metrics = current_metrics();

    MessageItemWidgets w{};
    w.btn = lv_btn_create(parent);
    lv_obj_set_size(w.btn, LV_PCT(100), metrics.list_item_height);
    lv_obj_add_flag(w.btn, LV_OBJ_FLAG_CLICKABLE);
    ::ui::components::two_pane_layout::make_non_scrollable(w.btn);

    w.name_label = lv_label_create(w.btn);
    lv_obj_add_flag(w.name_label, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_align(w.name_label, LV_ALIGN_LEFT_MID, metrics.name_x, 0);

    w.preview_label = lv_label_create(w.btn);
    lv_obj_add_flag(w.preview_label, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_align(w.preview_label, LV_ALIGN_LEFT_MID, metrics.preview_x, 0);
    lv_label_set_long_mode(w.preview_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(w.preview_label, metrics.preview_width);

    w.time_label = lv_label_create(w.btn);
    lv_obj_add_flag(w.time_label, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_align(w.time_label, LV_ALIGN_RIGHT_MID, -metrics.time_x, 0);

    w.unread_label = lv_label_create(w.btn);
    lv_obj_add_flag(w.unread_label, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_align(w.unread_label, LV_ALIGN_RIGHT_MID, -metrics.unread_x, 0);

    return w;
}

lv_obj_t* create_placeholder(lv_obj_t* parent)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    return label;
}

} // namespace chat::ui::layout

#endif
