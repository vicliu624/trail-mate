#include "ui/screens/settings/settings_page_runtime.h"

#include "app/app_facade_access.h"
#include "ui/screens/settings/settings_page_components.h"

namespace settings::ui::runtime
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

    settings::ui::components::create(parent);
}

void exit(lv_obj_t* parent)
{
    (void)parent;
    if (!is_available())
    {
        return;
    }

    settings::ui::components::destroy();
}

} // namespace settings::ui::runtime
