#include "ui_presentation/agenda/agenda_waypoint.h"
#include <cassert>
#include <cstring>

int main()
{
    ui::agenda::AgendaEditorModel draft;
    std::strcpy(draft.event.title, "Leave camp");
    waypoint::Record waypoint;
    waypoint.id = 0xFFFFFFFFu;
    waypoint.latitude_e7 = -900000000;
    waypoint.longitude_e7 = 1800000000;
    std::memset(waypoint.name, 'a', 31);
    assert(ui::agenda::applyWaypoint(draft, waypoint));
    assert(draft.dirty && draft.focus == ui::agenda::EditorField::Location);
    assert(draft.event.location_type == agenda::LocationType::Waypoint);
    assert(draft.event.flags & agenda::HasLocation);
    assert(!std::strcmp(draft.event.waypoint_id, "4294967295"));
    assert(!std::strcmp(draft.event.location_name, waypoint.name));
    assert(!std::strcmp(draft.event.title, "Leave camp"));
    waypoint = {}; // Source deletion cannot mutate the event snapshot.
    assert(draft.event.latitude_e7 == -900000000 && draft.event.longitude_e7 == 1800000000);
    const auto before = draft;
    assert(!ui::agenda::applyWaypoint(draft, waypoint));
    assert(!std::memcmp(&before, &draft, sizeof(draft)));
}
