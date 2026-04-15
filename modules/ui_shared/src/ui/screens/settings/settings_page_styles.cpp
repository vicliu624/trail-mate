/**
 * @file settings_page_styles.cpp
 * @brief Settings styles implementation
 */

#include "ui/screens/settings/settings_page_styles.h"
#include "ui/components/two_pane_styles.h"

namespace settings::ui::style
{
namespace
{

bool s_inited = false;
lv_style_t s_modal_bg;
lv_style_t s_modal_panel;
lv_style_t s_modal_btn_checked;
lv_style_t s_value_box;

} // namespace

void init_once()
{
    ::ui::components::two_pane_styles::init_once();
    if (s_inited) return;
    s_inited = true;

    lv_style_init(&s_modal_bg);
    lv_style_set_bg_opa(&s_modal_bg, LV_OPA_COVER);
    lv_style_set_bg_color(&s_modal_bg,
                          lv_color_hex(::ui::components::two_pane_styles::kSidePanelBg));

    lv_style_init(&s_modal_panel);
    lv_style_set_bg_opa(&s_modal_panel, LV_OPA_COVER);
    lv_style_set_bg_color(&s_modal_panel,
                          lv_color_hex(::ui::components::two_pane_styles::kMainPanelBg));
    lv_style_set_border_width(&s_modal_panel, 2);
    lv_style_set_border_color(&s_modal_panel,
                              lv_color_hex(::ui::components::two_pane_styles::kBorder));
    lv_style_set_radius(&s_modal_panel, 8);

    lv_style_init(&s_modal_btn_checked);
    lv_style_set_bg_opa(&s_modal_btn_checked, LV_OPA_COVER);
    lv_style_set_bg_color(&s_modal_btn_checked,
                          lv_color_hex(::ui::components::two_pane_styles::kAccent));

    lv_style_init(&s_value_box);
    lv_style_set_bg_opa(&s_value_box, LV_OPA_COVER);
    lv_style_set_bg_color(&s_value_box,
                          lv_color_hex(::ui::components::two_pane_styles::kSidePanelBg));
    lv_style_set_border_width(&s_value_box, 1);
    lv_style_set_border_color(&s_value_box,
                              lv_color_hex(::ui::components::two_pane_styles::kBorder));
    lv_style_set_radius(&s_value_box, 8);
}

void apply_panel_side(lv_obj_t* obj)
{
    ::ui::components::two_pane_styles::apply_panel_side(obj);
}

void apply_panel_main(lv_obj_t* obj)
{
    ::ui::components::two_pane_styles::apply_panel_main(obj);
}

void apply_btn_basic(lv_obj_t* btn)
{
    ::ui::components::two_pane_styles::apply_btn_basic(btn);
}

void apply_btn_filter(lv_obj_t* btn)
{
    ::ui::components::two_pane_styles::apply_btn_filter(btn);
}

void apply_list_item(lv_obj_t* item)
{
    ::ui::components::two_pane_styles::apply_list_item(item);
}

void apply_label_primary(lv_obj_t* label)
{
    ::ui::components::two_pane_styles::apply_label_primary(label);
}

void apply_label_muted(lv_obj_t* label)
{
    ::ui::components::two_pane_styles::apply_label_muted(label);
}

void apply_value_box(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_value_box, 0);
}

void apply_modal_bg(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_modal_bg, 0);
}

void apply_modal_panel(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_modal_panel, 0);
}

void apply_btn_modal(lv_obj_t* btn)
{
    init_once();
    ::ui::components::two_pane_styles::apply_btn_basic(btn);
    lv_obj_add_style(btn, &s_modal_btn_checked, LV_PART_MAIN | LV_STATE_CHECKED);
}

} // namespace settings::ui::style
