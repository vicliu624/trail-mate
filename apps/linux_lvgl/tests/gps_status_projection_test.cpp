#include "platform/ui/gps_runtime.h"
#include "ui/presentation_sources/runtime_gps_status_source.h"
#include <cassert>
#include <cstring>

namespace
{
bool enabled = true, powered = true;
gps::GpsState data;
gps::GpsDiagnosticsSnapshot diagnostic;
} // namespace
namespace sys
{
uint32_t millis_now() { return 1234; }
uint32_t epoch_seconds_now() { return 1792600000; }
} // namespace sys
namespace platform::ui::gps
{
bool is_enabled() { return enabled; }
bool is_powered() { return powered; }
GpsState get_data() { return data; }
GpsDiagnosticsSnapshot diagnostics() { return diagnostic; }
bool get_gnss_snapshot(GnssSatInfo*, std::size_t, std::size_t* count, GnssStatus* status)
{
    *count = 12;
    status->sats_in_view = status->sats_in_use = 12;
    status->hdop = 0.5;
    status->fix = GnssFix::FIX3D;
    return true;
}
} // namespace platform::ui::gps
int main()
{
    data.valid = data.has_alt = data.has_speed = data.has_course = true;
    data.lat = 1;
    data.lng = 2;
    data.alt_m = 100;
    data.speed_mps = 3;
    data.satellites = 12;
    diagnostic.ready = diagnostic.has_fix = true;
    diagnostic.sats_in_use = diagnostic.sats_in_view = 12;
    ui::presentation_sources::RuntimeGpsStatusSource source;
    ui::gps::GpsStatusSnapshot snapshot;
    const auto hidden = [&]
    {
        assert(source.buildGpsStatusSnapshot(snapshot));
        assert(!snapshot.receiver_ready && !snapshot.fix_valid && !snapshot.has_altitude);
        assert(snapshot.satellites == 0 && snapshot.hdop == 0 && snapshot.speed_mps == 0 && !snapshot.time_synced);
        assert(std::strcmp(snapshot.coordinate_label.c_str(), "--") == 0);
        assert(std::strcmp(snapshot.satellite_label.c_str(), "0 SAT") == 0);
    };
    diagnostic.code = gps::GpsDiagnosticCode::NotConfigured;
    hidden(); // Stale data must not turn an unconfigured receiver into a green FIX.
    assert(!snapshot.receiver_configured && std::strcmp(snapshot.fix_label.c_str(), "GPS N/A") == 0);
    diagnostic.code = gps::GpsDiagnosticCode::TransportNotReady;
    diagnostic.ready = false;
    hidden();
    assert(snapshot.receiver_configured && std::strcmp(snapshot.fix_label.c_str(), "NO RX") == 0);
    diagnostic.ready = true;
    diagnostic.code = gps::GpsDiagnosticCode::TrafficStalled;
    hidden();
    assert(std::strcmp(snapshot.fix_label.c_str(), "NO RX") == 0);
    diagnostic.code = gps::GpsDiagnosticCode::NoTraffic;
    hidden();
    enabled = false;
    hidden();
    assert(std::strcmp(snapshot.fix_label.c_str(), "GPS OFF") == 0);
    enabled = true;
    powered = false;
    hidden();
    assert(std::strcmp(snapshot.fix_label.c_str(), "GPS POWER") == 0);
    powered = true;
    diagnostic.code = gps::GpsDiagnosticCode::OK;
    assert(source.buildGpsStatusSnapshot(snapshot));
    assert(snapshot.receiver_ready && snapshot.fix_valid && snapshot.has_altitude && snapshot.satellites == 12);
    assert(std::strcmp(snapshot.fix_label.c_str(), "FIX") == 0);
}
