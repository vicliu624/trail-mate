#include "screens/tracker/tracker_page_components.h"

void ui_tracker_enter(lv_obj_t* parent)
{
    tracker::ui::components::init_page(parent);
}

void ui_tracker_exit(lv_obj_t*)
{
    tracker::ui::components::cleanup_page();
}
