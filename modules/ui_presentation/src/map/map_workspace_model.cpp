#include "ui_presentation/map/map_workspace_model.h"

#include <cmath>

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
    const auto result = sink_.setViewport(viewport);
    if (result.ok) viewport_ = viewport;
    return result;
}

ui::UiActionResult MapWorkspaceModel::setLayer(MapLayerKind layer, bool enabled)
{
    return sink_.setLayer(layer, enabled);
}

ui::UiActionResult MapWorkspaceModel::setActiveTool(MapToolKind tool)
{
    // Selection owns the tool until confirmed or cancelled. Ordinary pan and
    // zoom still use setViewport and must not silently abandon this mode.
    if (selection_.state == MapLocationSelectionState::Selecting && tool != MapToolKind::SelectLocation)
        return ui::UiActionResult::fail(ui::UiActionFailure::Busy);
    if (tool == MapToolKind::SelectLocation && selection_.state != MapLocationSelectionState::Selecting)
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    const auto result = sink_.setActiveTool(tool);
    if (result.ok) active_tool_ = tool;
    return result;
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

ui::UiActionResult MapWorkspaceModel::beginLocationSelection()
{
    if (selection_.state == MapLocationSelectionState::Selecting)
        return ui::UiActionResult::fail(ui::UiActionFailure::Busy);
    const auto result = sink_.setActiveTool(MapToolKind::SelectLocation);
    if (!result.ok) return result;
    previous_tool_ = active_tool_;
    active_tool_ = MapToolKind::SelectLocation;
    selection_ = {};
    selection_.state = MapLocationSelectionState::Selecting;
    return result;
}

ui::UiActionResult MapWorkspaceModel::pickLocation()
{
    if (selection_.state != MapLocationSelectionState::Selecting)
        return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    const auto view = snapshot();
    if (!view.header.valid) return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    const double lat = view.viewport.center_lat;
    const double lon = view.viewport.center_lon;
    if (!std::isfinite(lat) || !std::isfinite(lon) || lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0)
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    const auto result = sink_.setActiveTool(previous_tool_);
    if (!result.ok) return result;
    active_tool_ = previous_tool_;
    selection_.latitude = lat;
    selection_.longitude = lon;
    selection_.state = MapLocationSelectionState::Picked;
    return result;
}

ui::UiActionResult MapWorkspaceModel::cancelLocationSelection()
{
    if (selection_.state != MapLocationSelectionState::Selecting)
        return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    const auto result = sink_.setActiveTool(previous_tool_);
    if (!result.ok) return result;
    active_tool_ = previous_tool_;
    selection_ = {};
    selection_.state = MapLocationSelectionState::Cancelled;
    return result;
}

MapLocationSelection MapWorkspaceModel::locationSelection() const
{
    return selection_;
}

} // namespace ui::map
