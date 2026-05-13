#pragma once

#include "ui_presentation/workspace/presentation_workspace.h"
#include "ui_presentation/workspace/presentation_workspace_snapshot.h"

namespace ui::workspace
{

class PresentationWorkspaceProbe
{
  public:
    static PresentationWorkspaceSnapshot snapshot(
        PresentationWorkspace& workspace);
};

} // namespace ui::workspace
