#pragma once

#include "ui/team_actions/team_action_types.h"
#include "ui_presentation/common/ui_action_result.h"

namespace ui::team_actions
{

class ITeamActionSink
{
  public:
    virtual ~ITeamActionSink() = default;

    virtual ui::UiActionResult sendTeamAction(
        const TeamActionRequest& request) = 0;
};

class ITeamLocationSource
{
  public:
    virtual ~ITeamLocationSource() = default;

    virtual bool currentTeamLocation(TeamLocationSnapshot& out) = 0;
};

} // namespace ui::team_actions
