#include "fake/fake_map_action_sink.h"
#include "fake/fake_map_presentation_source.h"
#include "ui_presentation/map/map_target_request.h"

#include <cassert>
#include <cstring>
#include <limits>

int main()
{
    using namespace ui::map;
    ui::tests::FakeMapPresentationSource source;
    ui::tests::FakeMapActionSink sink;
    MapWorkspaceModel model(source, sink);
    MapTargetRequest request;
    // Zero is a real destination, not a request to follow the GPS fix.
    assert(request.valid() && request.focus(model, 12).ok);
    assert(model.viewport().center_lat == 0.0 && model.viewport().zoom == 12);
    request.viewport = {24.8731, 118.0, 14};
    ui::copyText(request.label, "Campsite");
    assert(request.focus(model, 12).ok);
    assert(model.viewport().center_lat == 24.8731 && model.viewport().zoom == 14);
    sink.set_viewport_result = ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    request.viewport.center_lat = 25.0;
    assert(!request.focus(model, 12).ok && model.viewport().center_lat == 24.8731);
    sink.set_viewport_result = ui::UiActionResult::success();
    assert(model.beginLocationSelection().ok);
    assert(!request.focus(model, 12).ok);
    assert(model.locationSelection().state == MapLocationSelectionState::Selecting);
    assert(model.cancelLocationSelection().ok);

    MapOverlaySnapshot snapshot;
    snapshot.item_count = 1;
    snapshot.items[0].kind = MapOverlayKind::CurrentPosition;
    request.appendOverlay(snapshot);
    assert(snapshot.item_count == 2 && !snapshot.truncated);
    assert(snapshot.items[0].kind == MapOverlayKind::CurrentPosition);
    const auto& target = snapshot.items[1];
    assert(target.kind == MapOverlayKind::SelectedTarget && target.selected && target.visible);
    assert(target.point.valid && target.point.lat == 25.0 && target.point.lon == 118.0);
    assert(std::strcmp(target.label.c_str(), "Campsite") == 0);
    // Full snapshots stay bounded and retain the self marker at the tail.
    snapshot.item_count = MapOverlaySnapshot::kMaxItems;
    for (auto& item : snapshot.items) item.kind = MapOverlayKind::TeamMember;
    snapshot.items[MapOverlaySnapshot::kMaxItems - 1].kind = MapOverlayKind::CurrentPosition;
    request.appendOverlay(snapshot);
    assert(snapshot.item_count == MapOverlaySnapshot::kMaxItems && snapshot.truncated);
    assert(snapshot.items[MapOverlaySnapshot::kMaxItems - 2].kind == MapOverlayKind::SelectedTarget);
    assert(snapshot.items[MapOverlaySnapshot::kMaxItems - 1].kind == MapOverlayKind::CurrentPosition);
    const double invalid[] = {-91.0, 91.0, std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()};
    for (double lat : invalid)
    {
        request.viewport.center_lat = lat;
        assert(!request.valid() && !request.focus(model, 12).ok);
        snapshot.item_count = 0;
        request.appendOverlay(snapshot);
        assert(snapshot.item_count == 0);
    }
    request.viewport = {0.0, 181.0, 12};
    assert(!request.valid());
    request.viewport = {-90.0, -180.0, 12};
    assert(request.valid());
    request.viewport = {90.0, 180.0, 12};
    assert(request.valid());
}
