#include "platform/ui/team_ui_store_runtime.h"
#include "ui/presentation_sources/team_map_overlay_source.h"
#include <cassert>

int main()
{
    team::ui::TeamUiSnapshot snapshot;
    snapshot.in_team = snapshot.has_team_id = true;
    snapshot.team_id = {0x51, 0x25, 0x39, 0x16, 0x11, 0x04, 0x01, 0x12};
    team::ui::TeamMemberUi member;
    member.node_id = 0xf48da2ae;
    member.color_index = 2;
    snapshot.members.push_back(member);
    team::ui::team_ui_snapshot_store().save(snapshot);
    const auto append = [&](uint32_t id, int32_t lat, uint32_t ts)
    {
        assert(team::ui::team_ui_posring_append(snapshot.team_id, id, lat, 1164000000, 0, 0, ts));
    };
    append(member.node_id, 250000000, 100);
    append(0x4be5ddef, 399000000, 101);
    append(member.node_id, 399100000, 102);
    append(member.node_id, 240000000, 99); // Delayed relays cannot move the marker backwards.
    std::vector<team::ui::TeamPosSample> latest;
    assert(team::ui::team_ui_posring_load_latest(snapshot.team_id, latest));
    assert(latest.size() == 2 && latest[0].member_id == member.node_id && latest[0].ts == 102 && latest[0].lat_e7 == 399100000);
    assert(latest[1].member_id == 0x4be5ddef && latest[1].ts == 101);
    ui::presentation_sources::TeamMapOverlaySource source(team::ui::team_ui_snapshot_store());
    ui::presentation_sources::TeamMapLocation location;
    assert(source.loadMemberLocation(member.node_id, location) && location.lat_e7 == 399100000 && location.ts == 102);
    ui::map_overlay::IMapOverlayTeamSource::TeamPoint points[4]{};
    assert(source.latestTeamPoints(points, 4) == 2 && points[0].valid && points[0].lat == 39.91);
    team::ui::team_ui_snapshot_store().clear();
}
