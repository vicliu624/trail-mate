#include "ui_map_runtime/map_overlay_snapshot_source.h"

#include <cassert>
#include <cstring>

namespace
{

class FakeGpsSource final : public ui::map_overlay::IMapOverlayGpsSource
{
  public:
    bool currentFix(double& out_lat, double& out_lon, bool& out_valid) const override
    {
        ++calls;
        if (!available)
        {
            return false;
        }
        out_lat = lat;
        out_lon = lon;
        out_valid = valid;
        return true;
    }

    mutable int calls = 0;
    bool available = true;
    double lat = 31.2304;
    double lon = 121.4737;
    bool valid = true;
};

class FakeTeamSource final : public ui::map_overlay::IMapOverlayTeamSource
{
  public:
    std::size_t latestTeamPoints(TeamPoint* out, std::size_t capacity) const override
    {
        ++calls;
        if (!available || capacity == 0)
        {
            return 0;
        }

        if (capacity >= 1)
        {
            out[0].node_id = 7;
            out[0].label = "Grace";
            out[0].lat = 52.4068;
            out[0].lon = -1.5197;
            out[0].valid = true;
        }
        if (capacity >= 2 && include_invalid)
        {
            out[1].node_id = 8;
            out[1].label = "Invalid";
            out[1].lat = 120.0;
            out[1].lon = 0.0;
            out[1].valid = true;
            return 2;
        }
        return 1;
    }

    mutable int calls = 0;
    bool available = true;
    bool include_invalid = false;
};

} // namespace

int main()
{
    FakeGpsSource gps;
    FakeTeamSource team;
    ui::map_overlay::MapOverlaySnapshotSource source(&gps, &team);

    ui::map::MapOverlaySnapshot snapshot;
    assert(source.buildMapOverlaySnapshot(snapshot));
    assert(snapshot.header.valid);
    assert(snapshot.header.version == 1);
    assert(snapshot.item_count == 2);
    assert(snapshot.items[0].kind == ui::map::MapOverlayKind::CurrentPosition);
    assert(snapshot.items[1].kind == ui::map::MapOverlayKind::TeamMember);
    assert(std::strcmp(snapshot.items[1].label.c_str(), "Grace") == 0);
    assert(gps.calls == 1);
    assert(team.calls == 1);

    gps.valid = false;
    team.include_invalid = true;
    assert(source.buildMapOverlaySnapshot(snapshot));
    assert(snapshot.item_count == 1);
    assert(snapshot.items[0].kind == ui::map::MapOverlayKind::TeamMember);

    gps.available = false;
    team.available = false;
    assert(source.buildMapOverlaySnapshot(snapshot));
    assert(snapshot.header.valid);
    assert(snapshot.item_count == 0);

    return 0;
}
