#pragma once

#include "ui_presentation/map/map_action_sink.h"
#include "ui_presentation/map/map_presentation_source.h"

namespace ui::map
{

class MapWorkspaceModel
{
  public:
    MapWorkspaceModel(IMapPresentationSource& source, IMapActionSink& sink);

    MapWorkspaceSnapshot snapshot() const;

    ui::UiActionResult centerOnSelf();
    ui::UiActionResult setViewport(const MapViewport& viewport);
    ui::UiActionResult setLayer(MapLayerKind layer, bool enabled);
    ui::UiActionResult setActiveTool(MapToolKind tool);
    ui::UiActionResult clearMeasurement();

    MapViewport viewport() const;
    MapToolKind activeTool() const;

  private:
    IMapPresentationSource& source_;
    IMapActionSink& sink_;

    MapViewport viewport_{};
    MapToolKind active_tool_ = MapToolKind::Pan;
};

} // namespace ui::map
