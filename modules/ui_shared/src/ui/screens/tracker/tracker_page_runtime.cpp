#include "ui/screens/tracker/tracker_page_runtime.h"

#include "app/app_facade_access.h"
#include "ui/screens/tracker/tracker_page_components.h"
#include "ui/screens/tracker/tracker_page_input.h"
#include "ui/ui_common.h"

namespace tracker::ui::runtime
{

bool is_available()
{
    return app::hasAppFacade();
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    (void)host;
    if (!is_available())
    {
        return;
    }

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);
    tracker::ui::components::init_page(parent);
    set_default_group(prev_group);
    tracker::ui::input::init_tracker_input();
}

void exit(lv_obj_t* parent)
{
    (void)parent;
    if (!is_available())
    {
        return;
    }

    tracker::ui::input::cleanup_tracker_input();
    tracker::ui::components::cleanup_page();
}

} // namespace tracker::ui::runtime