#include "ui/team_actions/team_action_types.h"

#include <cassert>

int main()
{
    ui::team_actions::TeamActionRequest action;
    assert(action.kind == ui::team_actions::TeamActionKind::Text);
    assert(action.text == nullptr);
    assert(action.location.marker_icon == 0);
    assert(action.command.kind == ui::team_actions::TeamCommandKind::Unknown);

    action.kind = ui::team_actions::TeamActionKind::LocationMarker;
    action.location.lat = 31.2304;
    action.location.lon = 121.4737;
    action.location.marker_icon = 4;
    assert(action.location.lat > 31.0);
    assert(action.location.marker_icon == 4);

    action.kind = ui::team_actions::TeamActionKind::Command;
    action.command.kind = ui::team_actions::TeamCommandKind::RallyPoint;
    action.command.payload = "rally";
    assert(action.command.kind == ui::team_actions::TeamCommandKind::RallyPoint);
    assert(action.command.payload != nullptr);
    return 0;
}
