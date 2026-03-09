#include "ui/components/two_pane_styles.h"

namespace ui::components::two_pane_styles
{
namespace
{

bool s_inited = false;

lv_style_t s_panel_side;
lv_style_t s_panel_main;
lv_style_t s_container_main;

lv_style_t s_btn_basic;
lv_style_t s_btn_filter_checked;
lv_style_t s_btn_filter_focused;

lv_style_t s_item_base;
lv_style_t s_item_focused;

lv_style_t s_label_primary;
lv_style_t s_label_muted;
lv_style_t s_label_accent;

} // namespace

void init_once()
{
    if (s_inited) return;
    s_inited = true;

    lv_style_init(&s_panel_side);
    lv_style_set_bg_opa(&s_panel_side, LV_OPA_COVER);
    lv_style_set_bg_color(&s_panel_side, lv_color_hex(kSidePanelBg));
    lv_style_set_border_width(&s_panel_side, 0);
    lv_style_set_pad_all(&s_panel_side, 3);
    lv_style_set_radius(&s_panel_side, 0);

    lv_style_init(&s_panel_main);
    lv_style_set_bg_opa(&s_panel_main, LV_OPA_COVER);
    lv_style_set_bg_color(&s_panel_main, lv_color_hex(kMainPanelBg));
    lv_style_set_border_width(&s_panel_main, 0);
    lv_style_set_pad_all(&s_panel_main, 3);
    lv_style_set_radius(&s_panel_main, 0);

    lv_style_init(&s_container_main);
    lv_style_set_bg_opa(&s_container_main, LV_OPA_COVER);
    lv_style_set_bg_color(&s_container_main, lv_color_hex(kMainPanelBg));
    lv_style_set_border_width(&s_container_main, 0);
    lv_style_set_pad_all(&s_container_main, 3);
    lv_style_set_radius(&s_container_main, 0);

    lv_style_init(&s_btn_basic);
    lv_style_set_bg_opa(&s_btn_basic, LV_OPA_COVER);
    lv_style_set_bg_color(&s_btn_basic, lv_color_hex(kMainPanelBg));
    lv_style_set_border_width(&s_btn_basic, 1);
    lv_style_set_border_color(&s_btn_basic, lv_color_hex(kBorder));
    lv_style_set_radius(&s_btn_basic, 12);
    lv_style_set_text_color(&s_btn_basic, lv_color_hex(kTextPrimary));

    lv_style_init(&s_btn_filter_checked);
    lv_style_set_bg_opa(&s_btn_filter_checked, LV_OPA_COVER);
    lv_style_set_bg_color(&s_btn_filter_checked, lv_color_hex(kAccent));

    lv_style_init(&s_btn_filter_focused);
    lv_style_set_bg_opa(&s_btn_filter_focused, LV_OPA_COVER);
    lv_style_set_bg_color(&s_btn_filter_focused, lv_color_hex(kAccent));

    lv_style_init(&s_item_base);
    lv_style_set_bg_opa(&s_item_base, LV_OPA_COVER);
    lv_style_set_bg_color(&s_item_base, lv_color_hex(kMainPanelBg));
    lv_style_set_border_width(&s_item_base, 1);
    lv_style_set_border_color(&s_item_base, lv_color_hex(kBorder));
    lv_style_set_radius(&s_item_base, 6);

    lv_style_init(&s_item_focused);
    lv_style_set_bg_opa(&s_item_focused, LV_OPA_COVER);
    lv_style_set_bg_color(&s_item_focused, lv_color_hex(kAccent));
    lv_style_set_outline_width(&s_item_focused, 0);

    lv_style_init(&s_label_primary);
    lv_style_set_text_color(&s_label_primary, lv_color_hex(kTextPrimary));

    lv_style_init(&s_label_muted);
    lv_style_set_text_color(&s_label_muted, lv_color_hex(kTextMuted));

    lv_style_init(&s_label_accent);
    lv_style_set_text_color(&s_label_accent, lv_color_hex(kAccent));
}

void apply_panel_side(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_panel_side, 0);
}

void apply_panel_main(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_panel_main, 0);
}

void apply_container_main(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_container_main, 0);
}

void apply_btn_basic(lv_obj_t* btn)
{
    init_once();
    lv_obj_add_style(btn, &s_btn_basic, LV_PART_MAIN);
}

void apply_btn_filter(lv_obj_t* btn)
{
    init_once();
    lv_obj_add_style(btn, &s_btn_basic, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_btn_filter_checked, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_add_style(btn, &s_btn_filter_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_style(btn, &s_btn_filter_focused, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
}

void apply_list_item(lv_obj_t* item)
{
    init_once();
    lv_obj_add_style(item, &s_item_base, LV_PART_MAIN);
    lv_obj_add_style(item, &s_item_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_style(item, &s_item_focused, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
}

void apply_label_primary(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_label_primary, 0);
}

void apply_label_muted(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_label_muted, 0);
}

void apply_label_accent(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_label_accent, 0);
}

} // namespace ui::components::two_pane_styles
