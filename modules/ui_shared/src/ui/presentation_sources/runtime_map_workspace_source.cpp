#include "ui/presentation_sources/runtime_map_workspace_source.h"

#include "sys/clock.h"
#include "ui/team_presence/team_presence_model.h"
#include "ui_presentation/common/fixed_text.h"

namespace ui::presentation_sources
{
namespace
{

constexpr uint32_t kMinValidEpoch = 1577836800U; // 2020-01-01

uint32_t presenceNowForMember(uint32_t last_seen_s)
{
    if (last_seen_s >= kMinValidEpoch)
    {
        return sys::epoch_seconds_now();
    }
    return sys::uptime_seconds_now();
}

void copyStatusLine(const ui::gps::GpsStatusSnapshot& gps,
                    ui::map::MapWorkspaceSnapshot& out)
{
    if (!gps.header.valid)
    {
        ui::copyText(out.status_line, "GPS unavailable");
        return;
    }

    if (!gps.receiver_enabled)
    {
        ui::copyText(out.status_line, "GPS off");
        return;
    }

    if (!gps.receiver_powered)
    {
        ui::copyText(out.status_line, "GPS power");
        return;
    }

    ui::copyText(out.status_line, gps.fix_valid ? "GPS fix" : "No GPS fix");
}

void projectTeamSummary(team::ui::ITeamUiSnapshotStore* store,
                        ui::map::TeamOverlaySummary& out)
{
    out = ui::map::TeamOverlaySummary{};
    if (store == nullptr)
    {
        return;
    }

    team::ui::TeamUiSnapshot snapshot;
    if (!store->load(snapshot) || !snapshot.in_team || !snapshot.has_team_id)
    {
        return;
    }

    out.available = true;
    for (const auto& member : snapshot.members)
    {
        ++out.visible_members;
        if (member.node_id != 0 &&
            !::ui::team_presence::isTeamMemberOnline(presenceNowForMember(member.last_seen_s),
                                                     member.last_seen_s))
        {
            ++out.stale_members;
        }
    }
}

} // namespace

RuntimeMapWorkspaceSource::RuntimeMapWorkspaceSource(
    ui::gps::IGpsStatusSource& gps_source,
    const RuntimeMapWorkspaceState& state,
    team::ui::ITeamUiSnapshotStore* team_store)
    : gps_source_(gps_source),
      state_(state),
      team_store_(team_store)
{
}

bool RuntimeMapWorkspaceSource::buildMapWorkspaceSnapshot(
    const ui::map::MapWorkspaceRequest& request,
    ui::map::MapWorkspaceSnapshot& out) const
{
    out = ui::map::MapWorkspaceSnapshot{};
    out.header.valid = true;
    out.header.version = 1;
    out.header.generated_at_ms = sys::millis_now();

    out.viewport = state_.has_viewport ? state_.last_viewport : request.requested_viewport;
    if (out.viewport.zoom == 0)
    {
        out.viewport.zoom = request.requested_viewport.zoom;
    }
    out.layers = state_.layers;
    out.measurement = state_.measurement;
    out.active_tool = request.active_tool;
    out.can_zoom_in = state_.can_zoom_in;
    out.can_zoom_out = state_.can_zoom_out;
    out.can_toggle_layers = state_.can_toggle_layers;

    ui::gps::GpsStatusSnapshot gps;
    if (gps_source_.buildGpsStatusSnapshot(gps))
    {
        out.self.valid = gps.fix_valid;
        out.self.lat = gps.latitude;
        out.self.lon = gps.longitude;
        out.self.course_deg = gps.course_deg;
        out.self.accuracy_m = gps.hdop;
        out.can_center_on_self = gps.fix_valid;
        if (gps.fix_valid && !state_.has_viewport)
        {
            out.viewport.center_lat = gps.latitude;
            out.viewport.center_lon = gps.longitude;
        }
        copyStatusLine(gps, out);
    }
    else
    {
        ui::copyText(out.status_line, "GPS unavailable");
    }

    projectTeamSummary(team_store_, out.team);
    return true;
}

RuntimeMapActionSink::RuntimeMapActionSink(ui::gps::IGpsStatusSource& gps_source,
                                           RuntimeMapWorkspaceState& state)
    : gps_source_(gps_source),
      state_(state)
{
}

ui::UiActionResult RuntimeMapActionSink::centerOnSelf()
{
    ui::gps::GpsStatusSnapshot gps;
    if (!gps_source_.buildGpsStatusSnapshot(gps) || !gps.header.valid || !gps.fix_valid)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    }

    state_.last_viewport.center_lat = gps.latitude;
    state_.last_viewport.center_lon = gps.longitude;
    state_.has_viewport = true;
    return ui::UiActionResult::success();
}

ui::UiActionResult RuntimeMapActionSink::setViewport(
    const ui::map::MapViewport& viewport)
{
    state_.last_viewport = viewport;
    state_.has_viewport = true;
    return ui::UiActionResult::success();
}

ui::UiActionResult RuntimeMapActionSink::setLayer(ui::map::MapLayerKind layer,
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
    }
    return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
}

ui::UiActionResult RuntimeMapActionSink::setActiveTool(ui::map::MapToolKind tool)
{
    state_.active_tool = tool;
    return ui::UiActionResult::success();
}

ui::UiActionResult RuntimeMapActionSink::clearMeasurement()
{
    state_.measurement = ui::map::MeasurementState{};
    return ui::UiActionResult::success();
}

RuntimeMapWorkspaceState& runtime_map_workspace_state()
{
    static RuntimeMapWorkspaceState state;
    return state;
}

} // namespace ui::presentation_sources
