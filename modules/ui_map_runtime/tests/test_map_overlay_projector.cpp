#include "ui_map_runtime/map_overlay/map_overlay_projector.h"

#include <cassert>
#include <cstddef>
#include <cstring>

namespace
{

void test_current_position()
{
    ui::map_overlay::MapOverlayProjector projector;
    ui::map::MapOverlaySnapshot snapshot{};

    assert(projector.projectCurrentPosition(31.2304, 121.4737, true, snapshot));
    assert(snapshot.item_count == 1);
    const auto& item = snapshot.items[0];
    assert(item.kind == ui::map::MapOverlayKind::CurrentPosition);
    assert(item.style == ui::map::MapOverlayStyle::OwnPosition);
    assert(item.point.valid);
    assert(item.point.lat == 31.2304);
    assert(item.point.lon == 121.4737);
    assert(std::strcmp(item.label.c_str(), "You") == 0);
    assert(std::strstr(item.detail.c_str(), "31.23040") != nullptr);
}

void test_team_member()
{
    ui::map_overlay::MapOverlayProjector projector;
    ui::map::MapOverlaySnapshot snapshot{};

    assert(projector.projectTeamMember(42, "Ada", 52.4068, -1.5197, true, snapshot));
    assert(snapshot.item_count == 1);
    const auto& item = snapshot.items[0];
    assert(item.kind == ui::map::MapOverlayKind::TeamMember);
    assert(item.style == ui::map::MapOverlayStyle::Team);
    assert(item.stable_id != 0);
    assert(std::strcmp(item.label.c_str(), "Ada") == 0);
    assert(std::strstr(item.detail.c_str(), "-1.51970") != nullptr);
}

void test_invalid_points_are_ignored()
{
    ui::map_overlay::MapOverlayProjector projector;
    ui::map::MapOverlaySnapshot snapshot{};

    assert(!projector.projectCurrentPosition(0.0, 0.0, false, snapshot));
    assert(!projector.projectCurrentPosition(100.0, 0.0, true, snapshot));
    assert(!projector.projectTeamMember(0, "bad", 1.0, 2.0, true, snapshot));
    assert(!projector.projectTeamMember(1, "bad", 1.0, 200.0, true, snapshot));
    assert(snapshot.item_count == 0);
    assert(!snapshot.truncated);
}

void test_capacity_sets_truncated()
{
    ui::map_overlay::MapOverlayProjector projector;
    ui::map::MapOverlaySnapshot snapshot{};

    for (std::size_t i = 0; i < ui::map::MapOverlaySnapshot::kMaxItems; ++i)
    {
        assert(projector.projectTeamMember(static_cast<uint32_t>(i + 1),
                                           "member",
                                           10.0,
                                           20.0,
                                           true,
                                           snapshot));
    }
    assert(snapshot.item_count == ui::map::MapOverlaySnapshot::kMaxItems);
    assert(!projector.projectTeamMember(99, "overflow", 10.0, 20.0, true, snapshot));
    assert(snapshot.truncated);
}

} // namespace

int main()
{
    test_current_position();
    test_team_member();
    test_invalid_points_are_ignored();
    test_capacity_sets_truncated();
    return 0;
}
