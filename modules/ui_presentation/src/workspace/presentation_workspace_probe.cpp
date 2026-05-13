#include "ui_presentation/workspace/presentation_workspace_probe.h"

namespace ui::workspace
{

PresentationWorkspaceSnapshot PresentationWorkspaceProbe::snapshot(
    PresentationWorkspace& workspace)
{
    PresentationWorkspaceSnapshot out{};
    out.header.valid = true;
    out.header.version = 1;

    if (workspace.device != nullptr)
    {
        out.has_device = true;
        out.device = workspace.device->snapshot();
    }

    if (workspace.gps != nullptr)
    {
        out.has_gps = true;
        out.gps = workspace.gps->snapshot();
    }

    if (workspace.mesh != nullptr)
    {
        out.has_mesh = true;
        out.mesh = workspace.mesh->snapshot();
    }

    if (workspace.settings != nullptr)
    {
        out.has_settings = true;
    }

    if (workspace.chat != nullptr)
    {
        out.has_chat = true;
    }

    if (workspace.team_chat != nullptr)
    {
        out.has_team_chat = true;
    }

    if (workspace.map != nullptr)
    {
        out.has_map = true;
        out.map = workspace.map->snapshot();
    }

    return out;
}

} // namespace ui::workspace
