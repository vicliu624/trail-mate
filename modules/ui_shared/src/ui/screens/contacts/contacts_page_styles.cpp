#include "ui/screens/contacts/contacts_page_styles.h"
#include "ui/components/two_pane_styles.h"

namespace contacts::ui::style
{

void init_once()
{
    ::ui::components::two_pane_styles::init_once();
}

void apply_panel_side(lv_obj_t* obj)
{
    ::ui::components::two_pane_styles::apply_panel_side(obj);
}

void apply_panel_main(lv_obj_t* obj)
{
    ::ui::components::two_pane_styles::apply_panel_main(obj);
}

void apply_container_white(lv_obj_t* obj)
{
    ::ui::components::two_pane_styles::apply_container_main(obj);
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

} // namespace contacts::ui::style
