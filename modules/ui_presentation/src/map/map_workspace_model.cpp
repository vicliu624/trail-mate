#include "ui_presentation/map/map_workspace_model.h"

namespace ui::map
{

MapWorkspaceModel::MapWorkspaceModel(IMapPresentationSource& source,
                                     IMapActionSink& sink)
    : source_(source),
      sink_(sink)
{
}

MapWorkspaceSnapshot MapWorkspaceModel::snapshot() const
{
    MapWorkspaceRequest request;
    request.requested_viewport = viewport_;
    request.active_tool = active_tool_;

    MapWorkspaceSnapshot out{};
    if (!source_.buildMapWorkspaceSnapshot(request, out))
    {
        out.header.valid = false;
    }
    return out;
}

ui::UiActionResult MapWorkspaceModel::centerOnSelf()
{
    const auto result = sink_.centerOnSelf();
    if (result.ok)
    {
        const auto snap = snapshot();
        if (snap.self.valid)
        {
            viewport_.center_lat = snap.self.lat;
            viewport_.center_lon = snap.self.lon;
        }
    }
    return result;
}

ui::UiActionResult MapWorkspaceModel::setViewport(const MapViewport& viewport)
{
    viewport_ = viewport;
    return sink_.setViewport(viewport);
}

ui::UiActionResult MapWorkspaceModel::setLayer(MapLayerKind layer, bool enabled)
{
    return sink_.setLayer(layer, enabled);
}

ui::UiActionResult MapWorkspaceModel::setActiveTool(MapToolKind tool)
{
    active_tool_ = tool;
    return sink_.setActiveTool(tool);
}

ui::UiActionResult MapWorkspaceModel::clearMeasurement()
{
    return sink_.clearMeasurement();
}

MapViewport MapWorkspaceModel::viewport() const
{
    return viewport_;
}

MapToolKind MapWorkspaceModel::activeTool() const
{
    return active_tool_;
}

} // namespace ui::map
