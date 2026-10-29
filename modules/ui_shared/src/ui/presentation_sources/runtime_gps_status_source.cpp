#include "ui/presentation_sources/runtime_gps_status_source.h"

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

    char label[32]{};
    std::snprintf(label,
                  sizeof(label),
                  "%.5f, %.5f",
                  out.latitude,
                  out.longitude);
    ui::copyText(out.coordinate_label, label);
}

void copySatelliteLabel(ui::gps::GpsStatusSnapshot& out,
                        const platform::ui::gps::GpsDiagnosticsSnapshot& diagnostics)
{
    char label[32]{};
    const unsigned in_use = diagnostics.sats_in_use > 0
                                ? diagnostics.sats_in_use
                                : static_cast<unsigned>(out.satellites);
    const unsigned in_view = diagnostics.sats_in_view;
    if (in_view > 0)
    {
        std::snprintf(label, sizeof(label), "%u/%u SAT", in_use, in_view);
    }
    else
    {
        std::snprintf(label, sizeof(label), "%u SAT", in_use);
    }
    ui::copyText(out.satellite_label, label);
}

} // namespace

bool RuntimeGpsStatusSource::buildGpsStatusSnapshot(ui::gps::GpsStatusSnapshot& out) const
{
    out = ui::gps::GpsStatusSnapshot{};
    out.header.valid = true;
    out.header.version = 1;
    out.header.generated_at_ms = sys::millis_now();

    out.receiver_enabled = ::platform::ui::gps::is_enabled();
    out.receiver_powered = ::platform::ui::gps::is_powered();

    const auto diagnostics = ::platform::ui::gps::diagnostics();
    const auto state = ::platform::ui::gps::get_data();
    out.receiver_configured = diagnostics.code != ::gps::GpsDiagnosticCode::NotConfigured;
    out.receiver_ready = out.receiver_enabled && out.receiver_powered && out.receiver_configured && diagnostics.ready &&
                         diagnostics.code != ::gps::GpsDiagnosticCode::NoTraffic && diagnostics.code != ::gps::GpsDiagnosticCode::TrafficStalled;

    out.fix_valid = out.receiver_ready && (state.valid || diagnostics.has_fix);
    out.latitude = out.receiver_ready ? state.lat : 0.0;
    out.longitude = out.receiver_ready ? state.lng : 0.0;
    out.has_altitude = out.fix_valid && state.has_alt;
    out.altitude_m = out.has_altitude ? static_cast<float>(state.alt_m) : 0.0f;
    out.speed_mps = out.fix_valid && state.has_speed ? static_cast<float>(state.speed_mps) : 0.0f;
    out.course_deg = out.fix_valid && state.has_course ? static_cast<float>(state.course_deg) : 0.0f;
    platform::ui::gps::GnssStatus gnss_status{};
    std::size_t gnss_count = 0;
    (void)::platform::ui::gps::get_gnss_snapshot(nullptr, 0, &gnss_count, &gnss_status);

    out.satellites = out.receiver_ready ? (diagnostics.sats_in_view > 0 ? diagnostics.sats_in_view : state.satellites) : 0;
    out.hdop = out.receiver_ready ? gnss_status.hdop : 0.0f;
    out.time_synced = out.fix_valid;
    out.epoch_seconds = out.time_synced ? static_cast<uint64_t>(sys::epoch_seconds_now()) : 0ULL;

    if (!out.receiver_enabled)
    {
        ui::copyText(out.fix_label, "GPS OFF");
    }
    else if (!out.receiver_configured)
    {
        ui::copyText(out.fix_label, "GPS N/A");
    }
    else if (!out.receiver_powered)
    {
        ui::copyText(out.fix_label, "GPS POWER");
    }
    else if (!out.receiver_ready)
    {
        ui::copyText(out.fix_label, "NO RX");
    }
    else
    {
        ui::copyText(out.fix_label, out.fix_valid ? "FIX" : "NO FIX");
    }

    copyCoordinateLabel(out);
    if (out.receiver_ready) copySatelliteLabel(out, diagnostics);
    else ui::copyText(out.satellite_label, "0 SAT");
    ui::copyText(out.time_label, out.time_synced ? "SYNCED" : "UNSYNCED");
    return true;
}

RuntimeGpsStatusSource& runtime_gps_status_source()
{
    static RuntimeGpsStatusSource source;
    return source;
}

} // namespace ui::presentation_sources
