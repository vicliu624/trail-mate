#include "boards/gat562_mesh_evb_pro/gat562_board.h"

#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"

#include <Arduino.h>

#include <cmath>

namespace platform::ui::device
{

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
    info.level = board.getBatteryLevel();
    return info;
}

void handle_low_battery(const BatteryInfo& info)
{
    (void)info;
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

int power_tier()
{
    return 0;
}

} // namespace platform::ui::device

namespace platform::ui::gps
{

GpsState get_data()
{
    return ::boards::gat562_mesh_evb_pro::Gat562Board::instance().gpsData();
}

bool get_gnss_snapshot(GnssSatInfo* out, std::size_t max, std::size_t* out_count, GnssStatus* status)
{
    return ::boards::gat562_mesh_evb_pro::Gat562Board::instance().gpsGnssSnapshot(out, max, out_count, status);
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
