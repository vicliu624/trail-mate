#include "fake/fake_map_action_sink.h"
#include "fake/fake_map_presentation_source.h"
#include "ui_presentation/common/fixed_text.h"
#include "ui_presentation/map/map_workspace_model.h"

#include <cassert>
#include <cstring>

namespace
{

ui::map::MapViewport viewport(double lat, double lon, uint8_t zoom)
{
    ui::map::MapViewport value;
    value.center_lat = lat;
    value.center_lon = lon;
    value.zoom = zoom;
    return value;
}

void assertViewport(const ui::map::MapViewport& actual,
                    const ui::map::MapViewport& expected)
{
    assert(actual.center_lat == expected.center_lat);
    assert(actual.center_lon == expected.center_lon);
    assert(actual.zoom == expected.zoom);
}

} // namespace

int main()
{
    ui::tests::FakeMapPresentationSource source;
    source.snapshot_value.header.valid = true;
    source.snapshot_value.header.version = 7;
    source.snapshot_value.self.valid = true;
    source.snapshot_value.self.lat = 52.4068;
    source.snapshot_value.self.lon = -1.5197;
    source.snapshot_value.self.accuracy_m = 3.5f;
    source.snapshot_value.layers.osm = true;
    source.snapshot_value.layers.contour = true;
    source.snapshot_value.team.available = true;
    source.snapshot_value.team.visible_members = 3;
    source.snapshot_value.team.stale_members = 1;
    source.snapshot_value.can_center_on_self = true;
    source.snapshot_value.can_zoom_in = true;
    source.snapshot_value.can_zoom_out = true;
    source.snapshot_value.can_toggle_layers = true;
    ui::copyText(source.snapshot_value.status_line, "GPS fix");

    ui::tests::FakeMapActionSink sink;
    ui::map::MapWorkspaceModel model(source, sink);

    const auto initial_viewport = viewport(52.0, -1.0, 12);
    const auto set_view = model.setViewport(initial_viewport);
    assert(set_view.ok);
    assert(sink.set_viewport_count == 1);
    assertViewport(sink.last_viewport, initial_viewport);
    assertViewport(model.viewport(), initial_viewport);

    const auto tool_result =
        model.setActiveTool(ui::map::MapToolKind::MeasureDistance);
    assert(tool_result.ok);
    assert(sink.set_active_tool_count == 1);
    assert(sink.last_tool == ui::map::MapToolKind::MeasureDistance);
    assert(model.activeTool() == ui::map::MapToolKind::MeasureDistance);

    const auto snap = model.snapshot();
    assert(snap.header.valid);
    assert(snap.header.version == 7);
    assert(snap.active_tool == ui::map::MapToolKind::MeasureDistance);
    assertViewport(snap.viewport, initial_viewport);
    assert(source.build_count == 1);
    assertViewport(source.last_request.requested_viewport, initial_viewport);
    assert(source.last_request.active_tool ==
           ui::map::MapToolKind::MeasureDistance);
    assert(snap.self.valid);
    assert(snap.self.lat == 52.4068);
    assert(snap.self.lon == -1.5197);
    assert(snap.layers.contour);
    assert(snap.team.available);
    assert(snap.team.visible_members == 3);
    assert(std::strcmp(snap.status_line.c_str(), "GPS fix") == 0);

    const auto layer_result =
        model.setLayer(ui::map::MapLayerKind::Terrain, true);
    assert(layer_result.ok);
    assert(sink.set_layer_count == 1);
    assert(sink.last_layer == ui::map::MapLayerKind::Terrain);
    assert(sink.last_layer_enabled);

    const auto clear_result = model.clearMeasurement();
    assert(clear_result.ok);
    assert(sink.clear_measurement_count == 1);

    const auto centered = model.centerOnSelf();
    assert(centered.ok);
    assert(sink.center_on_self_count == 1);
    assert(model.viewport().center_lat == 52.4068);
    assert(model.viewport().center_lon == -1.5197);
    assert(model.viewport().zoom == 12);

    const auto before_rejected = model.viewport();
    source.snapshot_value.self.lat = 10.0;
    source.snapshot_value.self.lon = 20.0;
    sink.center_on_self_result =
        ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    const auto rejected = model.centerOnSelf();
    assert(!rejected.ok);
    assert(rejected.failure == ui::UiActionFailure::Rejected);
    assertViewport(model.viewport(), before_rejected);

    sink.center_on_self_result = ui::UiActionResult::success();
    source.snapshot_value.self.valid = false;
    const auto no_fix_before = model.viewport();
    const auto no_fix = model.centerOnSelf();
    assert(no_fix.ok);
    assertViewport(model.viewport(), no_fix_before);

    source.available = false;
    const auto invalid_snapshot = model.snapshot();
    assert(!invalid_snapshot.header.valid);

    return 0;
}
