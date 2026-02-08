/**
 * @file ui_team.cpp
 * @brief Team page implementation
 */

#include "ui_team.h"
#include "../app/app_context.h"
#include "../sys/event_bus.h"
#include "screens/team/team_page_components.h"
#include "screens/team/team_state.h"
#include "ui_common.h"

namespace team
{
namespace ui
{
TeamPageState g_team_state;
} // namespace ui
} // namespace team

void ui_team_enter(lv_obj_t* parent)
{
    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);
    team_page_create(parent);
    if (lv_group_get_default() == nullptr)
    {
        set_default_group(prev_group);
    }
}

void ui_team_exit(lv_obj_t* parent)
{
    (void)parent;
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    if (controller)
    {
        controller->resetUiState();
    }
    team_page_destroy();
}

bool ui_team_handle_event(sys::Event* event)
{
    return team_page_handle_event(event);
}
