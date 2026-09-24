#include "fake/fake_map_action_sink.h"
#include "fake/fake_map_presentation_source.h"
#include "ui_presentation/map/map_workspace_model.h"

#include <cassert>
#include <limits>

int main()
{
    using namespace ui::map;
    ui::tests::FakeMapPresentationSource source;
    source.snapshot_value.header.valid = true;
    ui::tests::FakeMapActionSink sink;
    MapWorkspaceModel model(source, sink);
    assert(model.locationSelection().state == MapLocationSelectionState::Idle);
    assert(!model.pickLocation().ok && !model.cancelLocationSelection().ok);
    assert(!model.setActiveTool(MapToolKind::SelectLocation).ok);
    assert(model.setActiveTool(MapToolKind::MeasureDistance).ok);
    sink.set_active_tool_result = ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    assert(!model.beginLocationSelection().ok);
    assert(model.locationSelection().state == MapLocationSelectionState::Idle);
    assert(model.activeTool() == MapToolKind::MeasureDistance);
    assert(!model.setActiveTool(MapToolKind::Pan).ok);
    assert(model.activeTool() == MapToolKind::MeasureDistance);
    sink.set_active_tool_result = ui::UiActionResult::success();
    assert(model.beginLocationSelection().ok);
    assert(!model.beginLocationSelection().ok);
    assert(!model.setActiveTool(MapToolKind::Pan).ok);
    assert(model.snapshot().active_tool == MapToolKind::SelectLocation);
    assert(model.setViewport({24.8731, 118.0, 12}).ok);
    sink.set_viewport_result = ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    assert(!model.setViewport({50.0, 60.0, 7}).ok);
    assert(model.viewport().center_lat == 24.8731);
    sink.set_viewport_result = ui::UiActionResult::success();
    // Confirm the viewport, not the GPS fix, including zero coordinates.
    source.snapshot_value.self.valid = true;
    source.snapshot_value.self.lat = 50.0;
    source.snapshot_value.self.lon = 60.0;
    assert(model.pickLocation().ok);
    const auto picked = model.locationSelection();
    assert(picked.state == MapLocationSelectionState::Picked);
    assert(picked.latitude == 24.8731 && picked.longitude == 118.0);
    assert(model.activeTool() == MapToolKind::MeasureDistance);
    assert(!model.pickLocation().ok);
    assert(model.beginLocationSelection().ok);
    assert(model.locationSelection().latitude == 0.0);
    source.available = false;
    assert(!model.pickLocation().ok);
    source.available = true;
    const double invalid[] = {91.0, -91.0, std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity()};
    for (double lat : invalid)
    {
        assert(model.setViewport({lat, 0.0, 12}).ok);
        assert(!model.pickLocation().ok);
    }
    assert(model.setViewport({0.0, 181.0, 12}).ok);
    assert(!model.pickLocation().ok);
    assert(model.setViewport({0.0, 0.0, 12}).ok);
    sink.set_active_tool_result = ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    assert(!model.pickLocation().ok && !model.cancelLocationSelection().ok);
    assert(model.locationSelection().state == MapLocationSelectionState::Selecting);
    sink.set_active_tool_result = ui::UiActionResult::success();
    assert(model.pickLocation().ok);
    assert(model.locationSelection().latitude == 0.0 && model.locationSelection().longitude == 0.0);
    assert(model.beginLocationSelection().ok);
    assert(model.cancelLocationSelection().ok);
    assert(model.locationSelection().state == MapLocationSelectionState::Cancelled);
    assert(model.activeTool() == MapToolKind::MeasureDistance);
}
