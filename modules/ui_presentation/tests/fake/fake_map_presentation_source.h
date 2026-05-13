#pragma once

#include "ui_presentation/map/map_presentation_source.h"

namespace ui::tests
{

class FakeMapPresentationSource final : public ui::map::IMapPresentationSource
{
  public:
    bool buildMapWorkspaceSnapshot(
        const ui::map::MapWorkspaceRequest& request,
        ui::map::MapWorkspaceSnapshot& out) const override
    {
        ++build_count;
        last_request = request;

        if (!available)
        {
            return false;
        }

        out = snapshot_value;
        out.viewport = request.requested_viewport;
        out.active_tool = request.active_tool;
        return true;
    }

    bool available = true;
    ui::map::MapWorkspaceSnapshot snapshot_value{};

    mutable int build_count = 0;
    mutable ui::map::MapWorkspaceRequest last_request{};
};

} // namespace ui::tests
