#include "ui/presentation_sources/legacy_gps_status_source.h"

#include "platform/ui/gps_runtime.h"
#include "sys/clock.h"

#include <cstdio>

namespace ui::presentation_sources
{
namespace
{

void copyCoordinateLabel(ui::gps::GpsStatusSnapshot& out)
{
    if (!out.fix_valid)
    {
        ui::copyText(out.coordinate_label, "--");
        return;
    }

    char label[32];
    std::snprintf(label,
                  sizeof(label),
                  "%.5f, %.5f",
                  out.latitude,
                  out.longitude);
    ui::copyText(out.coordinate_label, label);
}

void copySatelliteLabel(ui::gps::GpsStatusSnapshot& out)
{
    char label[32];
    std::snprintf(label, sizeof(label), "%u SAT", static_cast<unsigned>(out.satellites));
    ui::copyText(out.satellite_label, label);
}

} // namespace

bool LegacyGpsStatusSource::buildGpsStatusSnapshot(ui::gps::GpsStatusSnapshot& out) const
{
    out = ui::gps::GpsStatusSnapshot{};
    out.header.valid = true;
    out.header.version = 1;
    out.header.generated_at_ms = sys::millis_now();

    out.receiver_enabled = ::platform::ui::gps::is_enabled();
    out.receiver_powered = ::platform::ui::gps::is_powered();

    const auto diagnostics = ::platform::ui::gps::diagnostics();
    const auto state = ::platform::ui::gps::get_data();

    out.fix_valid = state.valid || diagnostics.has_fix;
    out.latitude = state.lat;
    out.longitude = state.lng;
    out.altitude_m = static_cast<float>(state.alt_m);
    out.speed_mps = static_cast<float>(state.speed_mps);
    out.course_deg = static_cast<float>(state.course_deg);
    out.satellites = diagnostics.sats_in_view > 0 ? diagnostics.sats_in_view : state.satellites;
    out.hdop = 0.0f;
    out.time_synced = out.fix_valid;
    out.epoch_seconds = out.time_synced ? static_cast<uint64_t>(sys::epoch_seconds_now()) : 0ULL;

    if (!out.receiver_enabled)
    {
        ui::copyText(out.fix_label, "GPS OFF");
    }
    else if (!out.receiver_powered)
    {
        ui::copyText(out.fix_label, "GPS POWER");
    }
    else
    {
        ui::copyText(out.fix_label, out.fix_valid ? "FIX" : "NO FIX");
    }

    copyCoordinateLabel(out);
    copySatelliteLabel(out);
    ui::copyText(out.time_label, out.time_synced ? "SYNCED" : "UNSYNCED");
    return true;
}

LegacyGpsStatusSource& legacy_gps_status_source()
{
    static LegacyGpsStatusSource source;
    return source;
}

} // namespace ui::presentation_sources
