#include "boards/gat562_mesh_evb_pro/gat562_board.h"

#include "app/app_facade_access.h"
#include "chat/usecase/contact_service.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"

#include <Arduino.h>

#include <cmath>

namespace platform::ui::device
{

namespace
{

struct BatteryUiFilterState
{
    bool initialized = false;
    uint32_t last_sample_ms = 0;
    int stable_level = -1;
};

BatteryUiFilterState s_battery_filter{};

int applyBatteryUiFilter(int raw_level)
{
    if (raw_level < 0)
    {
        return raw_level;
    }

    constexpr uint32_t kSampleIntervalMs = 1500;
    constexpr int kDisplayHysteresis = 2;
    const uint32_t now_ms = millis();

    if (!s_battery_filter.initialized)
    {
        s_battery_filter.initialized = true;
        s_battery_filter.last_sample_ms = now_ms;
        s_battery_filter.stable_level = raw_level;
        return raw_level;
    }

    if ((now_ms - s_battery_filter.last_sample_ms) < kSampleIntervalMs)
    {
        return s_battery_filter.stable_level;
    }

    s_battery_filter.last_sample_ms = now_ms;
    const int delta = raw_level - s_battery_filter.stable_level;
    const int abs_delta = delta >= 0 ? delta : -delta;
    if (abs_delta >= kDisplayHysteresis)
    {
        s_battery_filter.stable_level = raw_level;
    }
    else
    {
        s_battery_filter.stable_level = (s_battery_filter.stable_level + raw_level) / 2;
    }

    return s_battery_filter.stable_level;
}

int readFilteredBatteryLevel(::boards::gat562_mesh_evb_pro::Gat562Board& board)
{
    constexpr int kBatterySamples = 6;
    int valid_sum = 0;
    int valid_count = 0;
    for (int i = 0; i < kBatterySamples; ++i)
    {
        const int sample = board.getBatteryLevel();
        if (sample >= 0)
        {
            valid_sum += sample;
            ++valid_count;
        }
        delay(2);
    }

    if (valid_count == 0)
    {
        return -1;
    }

    const int averaged = (valid_sum + (valid_count / 2)) / valid_count;
    return applyBatteryUiFilter(averaged);
}

} // namespace

void delay_ms(uint32_t ms)
{
    delay(ms);
}

void restart()
{
    NVIC_SystemReset();
}

bool rtc_ready()
{
    return ::boards::gat562_mesh_evb_pro::Gat562Board::instance().isRTCReady();
}

BatteryInfo battery_info()
{
    auto& board = ::boards::gat562_mesh_evb_pro::Gat562Board::instance();
    BatteryInfo info{};
    info.available = true;
    info.charging = board.isCharging();
    info.level = readFilteredBatteryLevel(board);
    return info;
}

MemoryStats memory_stats()
{
    MemoryStats stats{};
    return stats;
}

const char* firmware_version()
{
    return "unknown";
}

void handle_low_battery(const BatteryInfo& info)
{
    (void)info;
}

bool supports_screen_brightness()
{
    return false;
}

uint8_t screen_brightness()
{
    return ::boards::gat562_mesh_evb_pro::Gat562Board::instance().getBrightness();
}

void set_screen_brightness(uint8_t level)
{
    ::boards::gat562_mesh_evb_pro::Gat562Board::instance().setBrightness(level);
}

void trigger_haptic()
{
    ::boards::gat562_mesh_evb_pro::Gat562Board::instance().vibrator();
}

uint8_t default_message_tone_volume()
{
    return ::boards::gat562_mesh_evb_pro::Gat562Board::instance().getMessageToneVolume();
}

void set_message_tone_volume(uint8_t volume_percent)
{
    ::boards::gat562_mesh_evb_pro::Gat562Board::instance().setMessageToneVolume(volume_percent);
}

void play_message_tone()
{
    ::boards::gat562_mesh_evb_pro::Gat562Board::instance().playMessageTone();
}

bool sd_ready()
{
    return false;
}

bool card_ready()
{
    return false;
}

bool gps_ready()
{
    return ::boards::gat562_mesh_evb_pro::Gat562Board::instance().isGPSReady();
}

bool gps_supported()
{
    return ::boards::gat562_mesh_evb_pro::Gat562Board::instance().hasGPSHardware();
}

int power_tier()
{
    return ::boards::gat562_mesh_evb_pro::Gat562Board::instance().getPowerTier();
}

} // namespace platform::ui::device

namespace platform::ui::gps
{

namespace
{

void logGnssSnapshotUiFlow(bool has_snapshot, std::size_t sat_count, const GnssStatus& status)
{
    static bool s_last_has_snapshot = false;
    static std::size_t s_last_sat_count = static_cast<std::size_t>(-1);
    static uint8_t s_last_sats_in_view = 0xFF;
    static uint8_t s_last_sats_in_use = 0xFF;
    static uint32_t s_last_log_ms = 0;

    const uint32_t now_ms = millis();
    const bool changed = (has_snapshot != s_last_has_snapshot) || (sat_count != s_last_sat_count) ||
                         (status.sats_in_view != s_last_sats_in_view) || (status.sats_in_use != s_last_sats_in_use);
    const bool suspicious_full_scale =
        has_snapshot && sat_count == ::gps::kMaxGnssSats && status.sats_in_view != static_cast<uint8_t>(sat_count);
    if (!changed && !suspicious_full_scale && (now_ms - s_last_log_ms) < 2000U)
    {
        return;
    }

    s_last_has_snapshot = has_snapshot;
    s_last_sat_count = sat_count;
    s_last_sats_in_view = status.sats_in_view;
    s_last_sats_in_use = status.sats_in_use;
    s_last_log_ms = now_ms;

    Serial.printf("[gat562][gps][ui] snapshot has=%u count=%u status_view=%u status_use=%u hdop=%.1f fix=%u\n",
                  static_cast<unsigned>(has_snapshot ? 1 : 0),
                  static_cast<unsigned>(sat_count),
                  static_cast<unsigned>(status.sats_in_view),
                  static_cast<unsigned>(status.sats_in_use),
                  static_cast<double>(status.hdop),
                  static_cast<unsigned>(status.fix != ::gps::GnssFix::NOFIX ? 1 : 0));
}

} // namespace

GpsState get_data()
{
    GpsState gps = ::boards::gat562_mesh_evb_pro::Gat562Board::instance().gpsData();
    if (gps.valid)
    {
        return gps;
    }

    if (!::app::hasAppFacade())
    {
        return gps;
    }

    auto& facade = ::app::appFacade();
    const ::chat::NodeId self_id = facade.getSelfNodeId();
    if (self_id == 0)
    {
        return gps;
    }

    const ::chat::contacts::NodeInfo* self = facade.getContactService().getNodeInfo(self_id);
    if (!self || !self->position.valid)
    {
        return gps;
    }

    gps.valid = true;
    gps.lat = static_cast<double>(self->position.latitude_i) / 1e7;
    gps.lng = static_cast<double>(self->position.longitude_i) / 1e7;
    gps.has_alt = self->position.has_altitude;
    gps.alt_m = self->position.has_altitude ? static_cast<double>(self->position.altitude) : 0.0;
    gps.has_speed = false;
    gps.speed_mps = 0.0;
    gps.has_course = false;
    gps.course_deg = 0.0;
    gps.satellites = 0;
    gps.age = 0xFFFFFFFFUL;
    return gps;
}

bool get_gnss_snapshot(GnssSatInfo* out, std::size_t max, std::size_t* out_count, GnssStatus* status)
{
    GnssStatus local_status{};
    std::size_t local_count = 0;
    const bool has_snapshot = ::boards::gat562_mesh_evb_pro::Gat562Board::instance().gpsGnssSnapshot(
        out, max, out_count ? out_count : &local_count, status ? status : &local_status);
    const std::size_t final_count = out_count ? *out_count : local_count;
    const GnssStatus& final_status = status ? *status : local_status;
    logGnssSnapshotUiFlow(has_snapshot, final_count, final_status);
    return has_snapshot;
}

uint32_t last_motion_ms()
{
    return ::boards::gat562_mesh_evb_pro::Gat562Board::instance().gpsLastMotionMs();
}

bool is_enabled()
{
    return ::boards::gat562_mesh_evb_pro::Gat562Board::instance().gpsEnabled();
}

bool is_powered()
{
    return ::boards::gat562_mesh_evb_pro::Gat562Board::instance().gpsPowered();
}

void set_collection_interval(uint32_t interval_ms)
{
    ::boards::gat562_mesh_evb_pro::Gat562Board::instance().setGpsCollectionInterval(interval_ms);
}

void set_power_strategy(uint8_t strategy)
{
    ::boards::gat562_mesh_evb_pro::Gat562Board::instance().setGpsPowerStrategy(strategy);
}

void set_gnss_config(uint8_t mode, uint8_t sat_mask)
{
    ::boards::gat562_mesh_evb_pro::Gat562Board::instance().setGpsConfig(mode, sat_mask);
}

void set_nmea_config(uint8_t output_hz, uint8_t sentence_mask)
{
    ::boards::gat562_mesh_evb_pro::Gat562Board::instance().setGpsNmeaConfig(output_hz, sentence_mask);
}

void set_motion_idle_timeout(uint32_t timeout_ms)
{
    ::boards::gat562_mesh_evb_pro::Gat562Board::instance().setGpsMotionIdleTimeout(timeout_ms);
}

void set_motion_sensor_id(uint8_t sensor_id)
{
    ::boards::gat562_mesh_evb_pro::Gat562Board::instance().setGpsMotionSensorId(sensor_id);
}

void suspend_runtime()
{
    ::boards::gat562_mesh_evb_pro::Gat562Board::instance().suspendGps();
}

void resume_runtime()
{
    ::boards::gat562_mesh_evb_pro::Gat562Board::instance().resumeGps();
}

double calculate_map_resolution(int zoom, double lat)
{
    constexpr double kEarthCircumferenceM = 40075016.686;
    return (kEarthCircumferenceM * std::cos(lat * 3.14159265358979323846 / 180.0)) /
           (256.0 * static_cast<double>(1 << zoom));
}

} // namespace platform::ui::gps
