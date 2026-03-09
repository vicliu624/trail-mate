#include "ui/screens/team/team_page_runtime.h"

#include "app/app_facade_access.h"
#include "team/usecase/team_controller.h"
#include "ui/screens/team/team_page_components.h"

namespace team::ui::runtime
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

    team_page_create(parent);
}

void exit(lv_obj_t* parent)
{
    (void)parent;
    if (!is_available())
    {
        return;
    }

    team::TeamController* controller = app::teamFacade().getTeamController();
    if (controller)
    {
        controller->resetUiState();
    }
    team_page_destroy();
}

bool handle_event(sys::Event* event)
{
    if (!is_available())
    {
        (void)event;
        return false;
    }

    return team_page_handle_event(event);
}

} // namespace team::ui::runtime
