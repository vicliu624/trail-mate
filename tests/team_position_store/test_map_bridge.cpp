#include "platform/ui/team_ui_store_runtime.h"
#include "ui/presentation_sources/team_map_overlay_source.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unistd.h>
#include <vector>

namespace
{
uint32_t clock_ms = 1000;
}
namespace sys
{
uint32_t millis_now() { return clock_ms; }
} // namespace sys

int main()
{
    const auto root = std::filesystem::temp_directory_path() /
                      ("trailmate-team-map-bridge-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_TEST_SD_ROOT", root.c_str(), 1);
    team::ui::TeamUiSnapshot snapshot;
    snapshot.in_team = snapshot.has_team_id = true;
    snapshot.team_id = team::TeamId{1, 2, 3, 4, 5, 6, 7, 8};
    team::ui::TeamMemberUi cm0;
    cm0.node_id = 0xF48DA2AE;
    cm0.name = "CM0";
    cm0.color_index = 1;
    snapshot.members.push_back(cm0);
    team::ui::TeamMemberUi zero;
    zero.node_id = 0x4BE5DDEF;
    zero.name = "Zero";
    zero.color_index = 2;
    snapshot.members.push_back(zero);
    team::ui::TeamUiSnapshotMemoryStore roster;
    roster.save(snapshot);

    using Source = ui::presentation_sources::TeamMapOverlaySource;
    using Location = ui::presentation_sources::TeamMapLocation;
    using Point = ui::map_overlay::IMapOverlayTeamSource::TeamPoint;
    Location location;
    Point points[4]{};
    Source source(roster);
    assert(source.latestTeamPoints(points, 4) == 0);
    assert(team::ui::team_ui_posring_append(snapshot.team_id, cm0.node_id,
                                            250389000, 1027183000, 123, 0, 1700000000));
    std::filesystem::path ring;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        if (entry.path().filename() == "posring.log") ring = entry.path();
    assert(!ring.empty());

    // Use the exact short tail left by the old Arduino writer, not Linux's
    // in-memory position adapter. Merely opening the map must not rewrite it.
    std::filesystem::resize_file(ring, 48);
    const auto contents = [&]()
    {
        std::ifstream file(ring, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    const auto legacy = contents();
    clock_ms += 1001;
    assert(source.loadMemberLocation(cm0.node_id, location));
    assert(location.lat_e7 == 250389000 && location.lon_e7 == 1027183000);
    assert(location.alt_m == 123 && location.ts == 1700000000);
    assert(location.color == team::ui::team_color_from_index(1));
    assert(source.latestTeamPoints(points, 4) == 1);
    assert(points[0].node_id == cm0.node_id && points[0].valid);
    assert(std::strcmp(points[0].label, "A2AE") == 0);
    assert(std::abs(points[0].lat - 25.0389) < 1e-7);
    assert(std::abs(points[0].lon - 102.7183) < 1e-7);
    assert(contents() == legacy);

    // Append another member through the production SD adapter, then ensure
    // the shared map's normal cache expiry exposes both members' positions.
    assert(team::ui::team_ui_posring_append(snapshot.team_id, zero.node_id,
                                            250489000, 1027283000, 456, 0, 1700000030));
    clock_ms += 1001;
    assert(source.latestTeamPoints(points, 4) == 2);
    assert(source.loadMemberLocation(zero.node_id, location));
    assert(location.lat_e7 == 250489000 && location.lon_e7 == 1027283000);
    assert(location.alt_m == 456 && location.color == team::ui::team_color_from_index(2));
    assert(source.loadMemberLocation(cm0.node_id, location));
    assert(location.lat_e7 == 250389000 && location.lon_e7 == 1027183000);

    // A fresh presentation source must rebuild positions from SD; no previous
    // map instance or injected position cache is available to it.
    Source reopened(roster);
    Location locations[4]{};
    assert(reopened.latestTeamLocations(locations, 4) == 2);
    assert(reopened.loadMemberLocation(cm0.node_id, location));
    assert(reopened.loadMemberLocation(zero.node_id, location));
    assert(!reopened.loadMemberLocation(0x12345678, location));

    // Position history must not leak across team identities or an empty roster.
    auto another = snapshot;
    another.team_id[0] ^= 0x80;
    roster.save(another);
    assert(reopened.latestTeamPoints(points, 4) == 0);
    roster.clear();
    assert(!reopened.loadMemberLocation(cm0.node_id, location));
    std::puts("Production Arduino SD positions reach the shared map and member lookup; hardware RF acceptance remains separate.");
    std::filesystem::remove_all(root);
}
