#include "platform/ui/gps_runtime.h"
#include "ui/presentation_sources/runtime_gps_status_source.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

int main()
{
    namespace gps = platform::ui::gps;
    for (const char* variable : {"TRAIL_MATE_GPS_DEVICE", "TRAIL_MATE_GPS_NMEA_FILE", "TRAIL_MATE_GPS_VALID", "TRAIL_MATE_GPS_LAT", "TRAIL_MATE_GPS_LNG", "TRAIL_MATE_GPS_ALT_M", "TRAIL_MATE_GPS_SPEED_MPS", "TRAIL_MATE_GPS_COURSE_DEG", "TRAIL_MATE_GPS_SATS", "TRAIL_MATE_GPS_HDOP", "TRAIL_MATE_GPS_FIX", "TRAIL_MATE_GPS_DEMO_DEFAULTS"}) unsetenv(variable);
    setenv("TRAIL_MATE_GPS_AUTO_SERIAL", "0", 1);
    setenv("TRAIL_MATE_GPS_ENABLED", "1", 1);
    setenv("TRAIL_MATE_GPS_POWERED", "1", 1);
    gps::set_fallback_mode(gps::FallbackMode::LiveOnly);
    gps::set_enabled(true);
    ui::presentation_sources::RuntimeGpsStatusSource source;
    ui::gps::GpsStatusSnapshot snapshot;
    assert(gps::diagnostics().code == ::gps::GpsDiagnosticCode::NotConfigured);
    assert(std::strcmp(::gps::gpsDiagnosticCodeName(gps::diagnostics().code), "GPSD_NOT_CONFIGURED") == 0);
    assert(source.buildGpsStatusSnapshot(snapshot));
    assert(!snapshot.receiver_configured && !snapshot.receiver_ready && !snapshot.fix_valid);
    assert(std::strcmp(snapshot.fix_label.c_str(), "GPS N/A") == 0);

    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-readiness-test-missing", 1);
    assert(source.buildGpsStatusSnapshot(snapshot));
    assert(snapshot.receiver_configured && !snapshot.receiver_ready);
    assert(gps::diagnostics().code == ::gps::GpsDiagnosticCode::TransportNotReady);
    assert(std::strcmp(snapshot.fix_label.c_str(), "NO RX") == 0);
    unsetenv("TRAIL_MATE_GPS_DEVICE");
    setenv("TRAIL_MATE_GPS_AUTO_SERIAL", "1", 1);
    setenv("TRAIL_MATE_GPS_DEVICE_CANDIDATES", "/dev/trailmate-readiness-test-missing", 1);
    assert(gps::diagnostics().code == ::gps::GpsDiagnosticCode::TransportNotReady);
    setenv("TRAIL_MATE_GPS_AUTO_SERIAL", "0", 1);

    setenv("TRAIL_MATE_GPS_VALID", "1", 1);
    setenv("TRAIL_MATE_GPS_LAT", "1", 1);
    setenv("TRAIL_MATE_GPS_LNG", "2", 1);
    setenv("TRAIL_MATE_GPS_ALT_M", "12", 1);
    setenv("TRAIL_MATE_GPS_SPEED_MPS", "3", 1);
    setenv("TRAIL_MATE_GPS_SATS", "9", 1);
    assert(source.buildGpsStatusSnapshot(snapshot));
    assert(snapshot.receiver_configured && snapshot.receiver_ready && snapshot.fix_valid);
    assert(snapshot.latitude == 1 && snapshot.longitude == 2 && snapshot.has_altitude && snapshot.satellites == 9);
    assert(std::strcmp(snapshot.fix_label.c_str(), "FIX") == 0);
    gps::set_enabled(false);
    assert(source.buildGpsStatusSnapshot(snapshot));
    assert(!snapshot.receiver_ready && !snapshot.fix_valid && !snapshot.has_altitude && snapshot.satellites == 0);
    assert(snapshot.latitude == 0 && snapshot.speed_mps == 0 && !snapshot.time_synced);
    assert(std::strcmp(snapshot.fix_label.c_str(), "GPS OFF") == 0);
    assert(std::strcmp(snapshot.coordinate_label.c_str(), "--") == 0);
    gps::set_enabled(true);
    setenv("TRAIL_MATE_GPS_POWERED", "0", 1);
    assert(source.buildGpsStatusSnapshot(snapshot));
    assert(!snapshot.receiver_ready && !snapshot.fix_valid && snapshot.satellites == 0);
    assert(std::strcmp(snapshot.fix_label.c_str(), "GPS POWER") == 0);
    setenv("TRAIL_MATE_GPS_POWERED", "1", 1);
    setenv("TRAIL_MATE_GPS_VALID", "0", 1);
    assert(source.buildGpsStatusSnapshot(snapshot));
    assert(snapshot.receiver_ready && !snapshot.fix_valid && snapshot.satellites == 9);
    assert(std::strcmp(snapshot.fix_label.c_str(), "NO FIX") == 0);
    std::cout << "GNSS unconfigured, configured/unready, auto selection, disabled, power and fix status checks passed\n";
}
