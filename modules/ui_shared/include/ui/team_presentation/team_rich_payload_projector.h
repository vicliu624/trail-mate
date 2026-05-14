#pragma once

#include "platform/ui/team_ui_store_runtime.h"
#include "ui/team_presentation/team_rich_payload_display.h"

namespace ui::team_presentation
{

class TeamRichPayloadProjector
{
  public:
    bool project(const ::team::ui::TeamChatLogEntry& entry,
                 TeamRichPayloadDisplay& out) const;
};

} // namespace ui::team_presentation
