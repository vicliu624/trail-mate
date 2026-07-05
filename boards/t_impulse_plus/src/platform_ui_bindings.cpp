#include "boards/t_impulse_plus/t_impulse_plus_board.h"
#include "platform/ui/gps_runtime.h"

#include <cmath>

namespace platform::ui::gps
{

namespace
{

::boards::t_impulse_plus::TImpulsePlusBoard& board()
{
    return ::boards::t_impulse_plus::TImpulsePlusBoard::instance();
}

} // namespace

GpsState get_data()
{
    return board().gpsData();
}

bool get_gnss_snapshot(GnssSatInfo* out, std::size_t max, std::size_t* out_count, GnssStatus* status)
{
    return board().gpsGnssSnapshot(out, max, out_count, status);
}

GpsDiagnosticsSnapshot diagnostics()
{
    GpsDiagnosticsSnapshot snapshot{};
    snapshot.supported = board().hasGPSHardware();
    snapshot.enabled = is_enabled();
    snapshot.powered = is_powered();
    snapshot.ready = board().isGPSReady();

    const GpsState data = get_data();
    snapshot.has_fix = data.valid;
    snapshot.satellites = data.satellites;

    GnssStatus status{};
    std::size_t sat_count = 0;
    if (get_gnss_snapshot(nullptr, 0, &sat_count, &status))
    {
        snapshot.sats_in_view = status.sats_in_view;
        snapshot.sats_in_use = status.sats_in_use;
    }

    if (!snapshot.supported)
    {
        snapshot.code = ::gps::GpsDiagnosticCode::Disabled;
    }
    else if (!snapshot.enabled)
    {
        snapshot.code = ::gps::GpsDiagnosticCode::NotEnabled;
    }
    else if (!snapshot.powered)
    {
        snapshot.code = ::gps::GpsDiagnosticCode::PowerOff;
    }
    else if (!snapshot.ready)
    {
        snapshot.code = ::gps::GpsDiagnosticCode::TransportNotReady;
    }
    else if (!snapshot.has_fix)
    {
        snapshot.code = ::gps::GpsDiagnosticCode::NoFix;
    }
    else
    {
        snapshot.code = ::gps::GpsDiagnosticCode::OK;
    }
    return snapshot;
}

uint32_t last_motion_ms()
{
    return board().gpsLastMotionMs();
}

void tick_service()
{
    board().tickGps();
}

bool supports_receiver_baud_setting()
{
    return false;
}

bool supports_receiver_init_policy_settings()
{
    return false;
}

bool supports_gnss_runtime_settings()
{
    return false;
}

bool supports_collection_interval_setting()
{
    return true;
}

bool supports_external_nmea_output_setting()
{
    return false;
}

bool supports_altitude_reference_setting()
{
    return false;
}

bool supports_coordinate_format_setting()
{
    return false;
}

bool is_enabled()
{
    return board().gpsEnabled();
}

bool is_powered()
{
    return board().gpsPowered();
}

void set_enabled(bool enabled)
{
    board().setGpsEnabled(enabled);
}

void set_collection_interval(uint32_t interval_ms)
{
    board().setGpsCollectionInterval(interval_ms);
}

void set_power_strategy(uint8_t strategy)
{
    board().setGpsPowerStrategy(strategy);
}

void set_gnss_config(uint8_t mode, uint8_t sat_mask)
{
    board().setGpsConfig(mode, sat_mask);
}

void set_external_nmea_config(uint8_t output_hz, uint8_t sentence_mask)
{
    board().setGpsExternalNmeaConfig(output_hz, sentence_mask);
}

void set_receiver_init_config(const GpsReceiverInitConfig& config)
{
    (void)config;
}

void set_fallback_mode(FallbackMode mode)
{
    (void)mode;
}

void set_motion_idle_timeout(uint32_t timeout_ms)
{
    board().setGpsMotionIdleTimeout(timeout_ms);
}

void set_motion_sensor_id(uint8_t sensor_id)
{
    board().setGpsMotionSensorId(sensor_id);
}

void acquire_power_lease(const char* reason)
{
    (void)reason;
    board().resumeGps();
}

void release_power_lease(const char* reason)
{
    (void)reason;
}

void suspend_runtime()
{
    board().suspendGps();
}

void resume_runtime()
{
    board().resumeGps();
}

double calculate_map_resolution(int zoom, double lat)
{
    constexpr double kEarthCircumferenceM = 40075016.686;
    return (kEarthCircumferenceM * std::cos(lat * 3.14159265358979323846 / 180.0)) /
           (256.0 * static_cast<double>(1 << zoom));
}

} // namespace platform::ui::gps
