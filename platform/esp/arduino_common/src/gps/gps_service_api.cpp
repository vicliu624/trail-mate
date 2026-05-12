#include "platform/esp/arduino_common/gps/gps_service_api.h"
#include "platform/esp/arduino_common/gps/gps_service.h"
#include "sys/clock.h"
#include <cmath>
#include <ctime>

namespace gps
{
namespace
{
constexpr uint32_t kMinValidEpochSeconds = 1577836800UL; // 2020-01-01 UTC

class GpsServiceLocationSource final : public ILocationSource
{
  public:
    bool latestFix(LocationFix& out) const override
    {
        const GpsState state = gps_get_data();
        if (!state.valid)
        {
            out = {};
            return false;
        }
        uint64_t epoch = 0;
        const uint32_t now_epoch = sys::epoch_seconds_now();
        if (now_epoch >= kMinValidEpochSeconds)
        {
            epoch = now_epoch;
        }
        out = locationFixFromGpsState(state, sys::millis_now(), epoch);
        return true;
    }
};

class SystemEpochTimeAuthority final : public ITimeAuthority
{
  public:
    uint32_t nowMonotonicMs() const override
    {
        return sys::millis_now();
    }

    bool nowEpoch(uint64_t& out_epoch_seconds) const override
    {
        const uint32_t epoch = sys::epoch_seconds_now();
        if (epoch < kMinValidEpochSeconds)
        {
            out_epoch_seconds = 0;
            return false;
        }
        out_epoch_seconds = epoch;
        return true;
    }

    TimeSyncStatus syncStatus() const override
    {
        TimeSyncStatus status{};
        uint64_t epoch = 0;
        status.epoch_valid = nowEpoch(epoch);
        status.synced = status.epoch_valid;
        status.source = status.epoch_valid ? TimeSourceKind::System : TimeSourceKind::None;
        status.epoch_seconds = epoch;
        status.observed_at_ms = sys::millis_now();
        return status;
    }
};
} // namespace

GpsState gps_get_data()
{
    return GpsService::getInstance().getData();
}

bool gps_latest_fix(gps::LocationFix& out)
{
    return gps_location_source().latestFix(out);
}

const gps::ILocationSource& gps_location_source()
{
    static GpsServiceLocationSource source;
    return source;
}

const gps::ITimeAuthority& gps_time_authority()
{
    static SystemEpochTimeAuthority authority;
    return authority;
}

bool gps_get_gnss_snapshot(gps::GnssSatInfo* out, size_t max, size_t* out_count, gps::GnssStatus* status)
{
    return GpsService::getInstance().getGnssSnapshot(out, max, out_count, status);
}

GpsDiagnosticsSnapshot gps_get_diagnostics()
{
    return GpsService::getInstance().getDiagnostics();
}

uint32_t gps_get_last_motion_ms()
{
    return GpsService::getInstance().getLastMotionMs();
}

bool gps_is_enabled()
{
    return GpsService::getInstance().isEnabled();
}

bool gps_is_powered()
{
    return GpsService::getInstance().isPowered();
}

void gps_set_enabled(bool enabled)
{
    GpsService::getInstance().setEnabled(enabled);
}

void gps_set_collection_interval(uint32_t interval_ms)
{
    GpsService::getInstance().setCollectionInterval(interval_ms);
}

void gps_set_power_strategy(uint8_t strategy)
{
    GpsService::getInstance().setPowerStrategy(strategy);
}

void gps_set_gnss_config(uint8_t mode, uint8_t sat_mask)
{
    GpsService::getInstance().setGnssConfig(mode, sat_mask);
}

void gps_set_external_nmea_config(uint8_t output_hz, uint8_t sentence_mask)
{
    GpsService::getInstance().setExternalNmeaConfig(output_hz, sentence_mask);
}

void gps_set_receiver_init_config(const GpsReceiverInitConfig& config)
{
    GpsService::getInstance().setReceiverInitConfig(config);
}

void gps_set_motion_idle_timeout(uint32_t timeout_ms)
{
    GpsService::getInstance().setMotionIdleTimeout(timeout_ms);
}

void gps_set_motion_sensor_id(uint8_t sensor_id)
{
    GpsService::getInstance().setMotionSensorId(sensor_id);
}

TaskHandle_t gps_get_task_handle()
{
    return GpsService::getInstance().getTaskHandle();
}

double calculate_map_resolution(int zoom, double lat)
{
    const double MAX_LAT = 85.05112878;
    double lat_clamped = lat;
    if (lat_clamped > MAX_LAT) lat_clamped = MAX_LAT;
    if (lat_clamped < -MAX_LAT) lat_clamped = -MAX_LAT;

    double resolution_equator = 156543.03392 / std::pow(2.0, zoom);

    double lat_rad = lat_clamped * M_PI / 180.0;
    double resolution = resolution_equator * std::cos(lat_rad);

    return resolution;
}

} // namespace gps
