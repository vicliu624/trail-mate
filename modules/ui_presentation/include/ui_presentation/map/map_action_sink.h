#pragma once

#include "ui_presentation/common/ui_action_result.h"
#include "ui_presentation/map/map_workspace_snapshot.h"

namespace ui::map
{

class IMapActionSink
{
  public:
    virtual ~IMapActionSink() = default;

    virtual ui::UiActionResult centerOnSelf() = 0;
    virtual ui::UiActionResult setViewport(const MapViewport& viewport) = 0;
    virtual ui::UiActionResult setLayer(MapLayerKind layer, bool enabled) = 0;
    virtual ui::UiActionResult setActiveTool(MapToolKind tool) = 0;
    virtual ui::UiActionResult clearMeasurement() = 0;
};

} // namespace ui::map
