#include "ui/presentation_sources/legacy_map_action_sink.h"

namespace ui::presentation_sources
{

LegacyMapActionSink::LegacyMapActionSink(ui::gps::IGpsStatusSource& gps_source,
                                         LegacyMapPresentationState& state)
    : gps_source_(gps_source),
      state_(state)
{
}

ui::UiActionResult LegacyMapActionSink::centerOnSelf()
{
    ui::gps::GpsStatusSnapshot gps;
    if (!gps_source_.buildGpsStatusSnapshot(gps) || !gps.header.valid ||
        !gps.fix_valid)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    }

    return ui::UiActionResult::success();
}

ui::UiActionResult LegacyMapActionSink::setViewport(
    const ui::map::MapViewport& viewport)
{
    state_.last_viewport = viewport;
    state_.has_viewport = true;
    return ui::UiActionResult::success();
}

ui::UiActionResult LegacyMapActionSink::setLayer(ui::map::MapLayerKind layer,
                                                 bool enabled)
{
    switch (layer)
    {
    case ui::map::MapLayerKind::Osm:
        state_.layers.osm = enabled;
        return ui::UiActionResult::success();
    case ui::map::MapLayerKind::Terrain:
        state_.layers.terrain = enabled;
        return ui::UiActionResult::success();
    case ui::map::MapLayerKind::Satellite:
        state_.layers.satellite = enabled;
        return ui::UiActionResult::success();
    case ui::map::MapLayerKind::Contour:
        state_.layers.contour = enabled;
        return ui::UiActionResult::success();
    default:
        return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
    }
}

ui::UiActionResult LegacyMapActionSink::setActiveTool(ui::map::MapToolKind tool)
{
    state_.active_tool = tool;
    return ui::UiActionResult::success();
}

ui::UiActionResult LegacyMapActionSink::clearMeasurement()
{
    state_.measurement = ui::map::MeasurementState{};
    return ui::UiActionResult::success();
}

} // namespace ui::presentation_sources
