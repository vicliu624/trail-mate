#include "ui/presentation_sources/legacy_map_presentation_source.h"

#include "ui_presentation/common/fixed_text.h"

namespace ui::presentation_sources
{
namespace
{

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

void projectTeamSummary(team::ui::ITeamUiStore* store,
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
        if (!member.online)
        {
            ++out.stale_members;
        }
    }
}

} // namespace

LegacyMapPresentationSource::LegacyMapPresentationSource(
    ui::gps::IGpsStatusSource& gps_source,
    const LegacyMapPresentationState& state,
    team::ui::ITeamUiStore* team_store)
    : gps_source_(gps_source),
      state_(state),
      team_store_(team_store)
{
}

bool LegacyMapPresentationSource::buildMapWorkspaceSnapshot(
    const ui::map::MapWorkspaceRequest& request,
    ui::map::MapWorkspaceSnapshot& out) const
{
    out = ui::map::MapWorkspaceSnapshot{};
    out.header.valid = true;
    out.header.version = 1;

    out.viewport = request.requested_viewport;
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
        copyStatusLine(gps, out);
    }
    else
    {
        ui::copyText(out.status_line, "GPS unavailable");
    }

    projectTeamSummary(team_store_, out.team);
    return true;
}

LegacyMapPresentationState& legacy_map_presentation_state()
{
    static LegacyMapPresentationState state;
    return state;
}

} // namespace ui::presentation_sources
