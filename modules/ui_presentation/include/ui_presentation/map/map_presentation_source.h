#pragma once

#include "ui_presentation/map/map_workspace_snapshot.h"

namespace ui::map
{

struct MapWorkspaceRequest
{
    MapViewport requested_viewport;
    MapToolKind active_tool = MapToolKind::Pan;
};

class IMapPresentationSource
{
  public:
    virtual ~IMapPresentationSource() = default;

    virtual bool buildMapWorkspaceSnapshot(const MapWorkspaceRequest& request,
                                           MapWorkspaceSnapshot& out) const = 0;
};

} // namespace ui::map
