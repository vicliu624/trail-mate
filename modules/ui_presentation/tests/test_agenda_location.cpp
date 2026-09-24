#include "fake/fake_gps_status_source.h"
#include "ui_presentation/agenda/agenda_location_source.h"

#include <cassert>
#include <cstring>
#include <limits>

int main()
{
    using namespace ui::agenda;
    ui::tests::FakeGpsStatusSource gps;
    GpsAgendaLocationSource source(gps);
    AgendaCoordinate out{123, 456};
    assert(!source.currentLocation(out));
    assert(out.latitude_e7 == 123 && out.longitude_e7 == 456);
    gps.snapshot_value.header.valid = true;
    gps.snapshot_value.receiver_enabled = true;
    gps.snapshot_value.receiver_powered = true;
    gps.snapshot_value.fix_valid = true;
    gps.snapshot_value.latitude = 24.87310004;
    gps.snapshot_value.longitude = 118.00000006;
    assert(source.currentLocation(out));
    assert(out.latitude_e7 == 248731000 && out.longitude_e7 == 1180000001);
    gps.snapshot_value.receiver_powered = false;
    assert(!source.currentLocation(out));
    gps.snapshot_value.receiver_powered = true;
    gps.available = false;
    assert(!source.currentLocation(out));
    gps.available = true;
    gps.snapshot_value.latitude = std::numeric_limits<double>::quiet_NaN();
    assert(!source.currentLocation(out));
    assert(!coordinateFromDegrees(91, 0, out));
    assert(!coordinateFromDegrees(0, -181, out));
    assert(coordinateFromDegrees(-90, 180, out));
    assert(out.latitude_e7 == -900000000 && out.longitude_e7 == 1800000000);
    AgendaEditorModel draft;
    std::strcpy(draft.event.title, "Keep title");
    std::strcpy(draft.event.note, "Keep note");
    std::strcpy(draft.event.waypoint_id, "old-waypoint");
    draft.event.flags = ::agenda::HasNote | ::agenda::HasReminder;
    draft.event.start_time = 1776330000;
    assert(applyCoordinate(draft, out));
    assert(draft.dirty && draft.focus == EditorField::Location);
    assert(draft.event.waypoint_id[0] == '\0');
    assert(draft.event.location_type == ::agenda::LocationType::Coordinate);
    assert(std::strcmp(draft.event.location_name, "-90.00000, 180.00000") == 0);
    assert(!applyCoordinate(draft, {900000001, 0}));
    assert(draft.event.latitude_e7 == -900000000);
    clearLocation(draft);
    assert(draft.event.flags == (::agenda::HasNote | ::agenda::HasReminder));
    assert(draft.event.location_type == ::agenda::LocationType::None);
    assert(draft.event.latitude_e7 == 0 && draft.event.longitude_e7 == 0);
    assert(draft.event.location_name[0] == '\0' && draft.event.waypoint_id[0] == '\0');
    assert(draft.event.start_time == 1776330000);
    assert(std::strcmp(draft.event.title, "Keep title") == 0);
    assert(std::strcmp(draft.event.note, "Keep note") == 0);
    assert(coordinateFromDegrees(0, 0, out));
    assert(applyCoordinate(draft, out));
    assert(draft.event.flags & ::agenda::HasLocation);
}
