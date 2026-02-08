#include "screens/tracker/tracker_page_components.h"
#include "ui_common.h"

void ui_tracker_enter(lv_obj_t* parent)
{
    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);
    tracker::ui::components::init_page(parent);
    extern lv_group_t* app_g;
    if (app_g)
    {
        set_default_group(app_g);
        lv_group_set_editing(app_g, false);
    }
    else
    {
        set_default_group(prev_group);
    }
}

void ui_tracker_exit(lv_obj_t*)
{
    tracker::ui::components::cleanup_page();
}
