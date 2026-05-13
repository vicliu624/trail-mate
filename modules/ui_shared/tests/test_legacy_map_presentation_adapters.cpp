#include "platform/ui/team_ui_store_runtime.h"
#include "ui/presentation_sources/legacy_map_action_sink.h"
#include "ui/presentation_sources/legacy_map_presentation_source.h"
#include "ui_presentation/common/fixed_text.h"

#include <cassert>
#include <cstring>

namespace
{

class FakeGpsStatusSource final : public ui::gps::IGpsStatusSource
{
  public:
    bool buildGpsStatusSnapshot(ui::gps::GpsStatusSnapshot& out) const override
    {
        ++build_count;
        if (!available)
        {
            return false;
        }

        out = snapshot;
        return true;
    }

    mutable int build_count = 0;
    bool available = true;
    ui::gps::GpsStatusSnapshot snapshot{};
};

class FakeTeamStore final : public team::ui::ITeamUiStore
{
  public:
    bool load(team::ui::TeamUiSnapshot& out) override
    {
        ++load_count;
        if (!available)
        {
            return false;
        }

        out = snapshot;
        return true;
    }

    void save(const team::ui::TeamUiSnapshot& in) override
    {
        snapshot = in;
        available = true;
    }

    void clear() override
    {
        snapshot = team::ui::TeamUiSnapshot{};
        available = false;
    }

    int load_count = 0;
    bool available = true;
    team::ui::TeamUiSnapshot snapshot{};
};

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
    FakeGpsStatusSource gps;
    gps.snapshot.header.valid = true;
    gps.snapshot.receiver_enabled = true;
    gps.snapshot.receiver_powered = true;
    gps.snapshot.fix_valid = true;
    gps.snapshot.latitude = 52.4068;
    gps.snapshot.longitude = -1.5197;
    gps.snapshot.course_deg = 180.0f;
    gps.snapshot.hdop = 2.25f;
    ui::copyText(gps.snapshot.fix_label, "FIX");

    FakeTeamStore team_store;
    team_store.snapshot.in_team = true;
    team_store.snapshot.has_team_id = true;
    team::ui::TeamMemberUi ada;
    ada.node_id = 1;
    ada.name = "Ada";
    ada.online = true;
    team::ui::TeamMemberUi grace;
    grace.node_id = 2;
    grace.name = "Grace";
    grace.online = false;
    team_store.snapshot.members.push_back(ada);
    team_store.snapshot.members.push_back(grace);

    ui::presentation_sources::LegacyMapPresentationState state;
    state.layers.osm = true;
    state.layers.contour = true;
    state.measurement.active = true;
    state.measurement.distance_m = 42.0f;

    ui::presentation_sources::LegacyMapPresentationSource source(gps,
                                                                 state,
                                                                 &team_store);
    ui::presentation_sources::LegacyMapActionSink sink(gps, state);

    ui::map::MapWorkspaceRequest request;
    request.requested_viewport = viewport(52.0, -1.0, 12);
    request.active_tool = ui::map::MapToolKind::MeasureDistance;

    ui::map::MapWorkspaceSnapshot snapshot;
    assert(source.buildMapWorkspaceSnapshot(request, snapshot));
    assert(snapshot.header.valid);
    assert(snapshot.header.version == 1);
    assertViewport(snapshot.viewport, request.requested_viewport);
    assert(snapshot.active_tool == ui::map::MapToolKind::MeasureDistance);
    assert(snapshot.layers.osm);
    assert(snapshot.layers.contour);
    assert(snapshot.measurement.active);
    assert(snapshot.measurement.distance_m == 42.0f);
    assert(snapshot.self.valid);
    assert(snapshot.self.lat == 52.4068);
    assert(snapshot.self.lon == -1.5197);
    assert(snapshot.self.course_deg == 180.0f);
    assert(snapshot.self.accuracy_m == 2.25f);
    assert(snapshot.can_center_on_self);
    assert(snapshot.team.available);
    assert(snapshot.team.visible_members == 2);
    assert(snapshot.team.stale_members == 1);
    assert(std::strcmp(snapshot.status_line.c_str(), "GPS fix") == 0);

    const auto center = sink.centerOnSelf();
    assert(center.ok);

    const auto updated_viewport = viewport(53.0, -2.0, 10);
    assert(sink.setViewport(updated_viewport).ok);
    assert(state.has_viewport);
    assertViewport(state.last_viewport, updated_viewport);

    assert(sink.setLayer(ui::map::MapLayerKind::Terrain, true).ok);
    assert(state.layers.terrain);
    assert(sink.setLayer(ui::map::MapLayerKind::Contour, false).ok);
    assert(!state.layers.contour);

    assert(sink.setActiveTool(ui::map::MapToolKind::Pan).ok);
    assert(state.active_tool == ui::map::MapToolKind::Pan);

    assert(sink.clearMeasurement().ok);
    assert(!state.measurement.active);

    gps.snapshot.fix_valid = false;
    const auto no_fix_center = sink.centerOnSelf();
    assert(!no_fix_center.ok);
    assert(no_fix_center.failure == ui::UiActionFailure::NotReady);

    gps.available = false;
    assert(source.buildMapWorkspaceSnapshot(request, snapshot));
    assert(snapshot.header.valid);
    assert(!snapshot.self.valid);
    assert(!snapshot.can_center_on_self);
    assert(std::strcmp(snapshot.status_line.c_str(), "GPS unavailable") == 0);

    team_store.available = false;
    gps.available = true;
    assert(source.buildMapWorkspaceSnapshot(request, snapshot));
    assert(!snapshot.team.available);

    return 0;
}
