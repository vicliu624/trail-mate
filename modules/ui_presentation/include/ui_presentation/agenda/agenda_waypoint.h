#pragma once
#include "ui_presentation/agenda/agenda_editor_model.h"
#include "waypoint/waypoint.h"
#include <cstdio>
#include <cstring>

namespace ui::agenda
{
// Copy a durable waypoint into a draft. Its location remains valid if the
// saved waypoint is later renamed or deleted. This never persists the event.
inline bool applyWaypoint(AgendaEditorModel& draft, const ::waypoint::Record& waypoint)
{
    if (!::waypoint::valid(waypoint)) return false;
    auto& event = draft.event;
    event.latitude_e7 = waypoint.latitude_e7;
    event.longitude_e7 = waypoint.longitude_e7;
    event.location_type = ::agenda::LocationType::Waypoint;
    event.flags |= ::agenda::HasLocation;
    std::memset(event.location_name, 0, sizeof(event.location_name));
    std::memcpy(event.location_name, waypoint.name, std::strlen(waypoint.name));
    std::memset(event.waypoint_id, 0, sizeof(event.waypoint_id));
    std::snprintf(event.waypoint_id, sizeof(event.waypoint_id), "%lu", static_cast<unsigned long>(waypoint.id));
    draft.dirty = true;
    draft.focus = EditorField::Location;
    return true;
}
} // namespace ui::agenda
