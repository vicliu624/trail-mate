#pragma once

#include "ui_presentation/agenda/agenda_editor_model.h"
#include "ui_presentation/gps/gps_status_source.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace ui::agenda
{
struct AgendaCoordinate
{
    int32_t latitude_e7 = 0;
    int32_t longitude_e7 = 0;
};

inline bool coordinateFromDegrees(double latitude, double longitude, AgendaCoordinate& out)
{
    if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
        latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0)
        return false;
    out.latitude_e7 = static_cast<int32_t>(std::lround(latitude * 1e7));
    out.longitude_e7 = static_cast<int32_t>(std::lround(longitude * 1e7));
    return true;
}

class IAgendaLocationSource
{
  public:
    virtual ~IAgendaLocationSource() = default;
    virtual bool currentLocation(AgendaCoordinate& out) const = 0;
};

// Portable adapter: the target supplies the existing GPS presentation source.
// No GPS hardware dependency and no retained snapshot or polling timer.
class GpsAgendaLocationSource final : public IAgendaLocationSource
{
  public:
    explicit GpsAgendaLocationSource(const ::ui::gps::IGpsStatusSource& source) : source_(source) {}
    bool currentLocation(AgendaCoordinate& out) const override
    {
        ::ui::gps::GpsStatusSnapshot snapshot;
        if (!source_.buildGpsStatusSnapshot(snapshot) || !snapshot.header.valid ||
            !snapshot.receiver_enabled || !snapshot.receiver_powered || !snapshot.fix_valid)
            return false;
        return coordinateFromDegrees(snapshot.latitude, snapshot.longitude, out);
    }

  private:
    const ::ui::gps::IGpsStatusSource& source_;
};

inline bool applyCoordinate(AgendaEditorModel& draft, const AgendaCoordinate& coordinate)
{
    if (coordinate.latitude_e7 < -900000000 || coordinate.latitude_e7 > 900000000 ||
        coordinate.longitude_e7 < -1800000000 || coordinate.longitude_e7 > 1800000000)
        return false;
    auto& event = draft.event;
    event.latitude_e7 = coordinate.latitude_e7;
    event.longitude_e7 = coordinate.longitude_e7;
    event.location_type = ::agenda::LocationType::Coordinate;
    event.flags |= ::agenda::HasLocation;
    std::memset(event.waypoint_id, 0, sizeof(event.waypoint_id));
    // Persist data, not a translation of "Current position" tied to one locale.
    std::snprintf(event.location_name, sizeof(event.location_name), "%.5f, %.5f",
                  coordinate.latitude_e7 / 1e7, coordinate.longitude_e7 / 1e7);
    draft.dirty = true;
    draft.focus = EditorField::Location;
    return true;
}

inline void clearLocation(AgendaEditorModel& draft)
{
    auto& event = draft.event;
    draft.dirty |= (event.flags & ::agenda::HasLocation) != 0;
    event.flags &= ~::agenda::HasLocation;
    event.latitude_e7 = event.longitude_e7 = 0;
    event.location_type = ::agenda::LocationType::None;
    std::memset(event.location_name, 0, sizeof(event.location_name));
    std::memset(event.waypoint_id, 0, sizeof(event.waypoint_id));
    draft.focus = EditorField::Location;
}
} // namespace ui::agenda
