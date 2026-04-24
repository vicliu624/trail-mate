#include "platform/ui/device_runtime.h"
#include "platform/ui/firmware_update_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/hostlink_runtime.h"
#include "platform/ui/lora_runtime.h"
#include "platform/ui/orientation_runtime.h"
#include "platform/ui/route_storage.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/sstv_runtime.h"
#include "platform/ui/time_runtime.h"
#include "platform/ui/tracker_runtime.h"
#include "platform/ui/usb_support_runtime.h"
#include "platform/ui/walkie_runtime.h"
#include "platform/ui/wifi_runtime.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

int main()
{
    using namespace platform::ui;
    namespace ui_gps = platform::ui::gps;
    namespace ui_hostlink = platform::ui::hostlink;

    settings_store::clear_namespace("settings");
    settings_store::clear_namespace("runtime_smoke");

    settings_store::put_int("runtime_smoke", "answer", 42);
    settings_store::put_bool("runtime_smoke", "enabled", true);
    settings_store::put_uint("runtime_smoke", "timeout", 1234U);
    const std::string greeting = "hello";
    assert(settings_store::put_string("runtime_smoke", "greeting", greeting.c_str()));

    const std::vector<uint8_t> blob_in = {1U, 2U, 3U, 4U};
    assert(settings_store::put_blob("runtime_smoke", "blob", blob_in.data(), blob_in.size()));

    std::string greeting_out;
    std::vector<uint8_t> blob_out;
    assert(settings_store::get_int("runtime_smoke", "answer", 0) == 42);
    assert(settings_store::get_bool("runtime_smoke", "enabled", false));
    assert(settings_store::get_uint("runtime_smoke", "timeout", 0) == 1234U);
    assert(settings_store::get_string("runtime_smoke", "greeting", greeting_out));
    assert(greeting_out == greeting);
    assert(settings_store::get_blob("runtime_smoke", "blob", blob_out));
    assert(blob_out == blob_in);

    screen::set_timeout_ms(45000U);
    assert(screen::clamp_timeout_ms(45000U) == 45000U);
    assert(screen::timeout_ms() == 45000U);
    assert(screen::timeout_secs() == 45U);

    screen::init({});
    screen::disable_sleep();
    assert(screen::is_sleep_disabled());
    screen::enable_sleep();
    assert(!screen::is_sleep_disabled());

    time::set_timezone_offset_min(60);
    const std::time_t shifted = time::apply_timezone_offset(3600);
    assert(shifted == 7200);

    std::tm local_tm{};
    (void)time::localtime_now(&local_tm);

    const auto version = device::firmware_version();
    assert(version != nullptr);
    assert(version[0] != '\0');
    assert(device::rtc_ready());
    device::delay_ms(1);

    assert(!firmware_update::is_supported());
    const auto firmware_status = firmware_update::status();
    assert(!firmware_status.supported);
    assert(firmware_status.phase == firmware_update::Phase::Unsupported);
    assert(!firmware_update::start_check());
    assert(!firmware_update::start_install());

    assert(!ui_hostlink::is_supported());
    ui_hostlink::start();
    assert(!ui_hostlink::is_active());
    const auto hostlink_status = ui_hostlink::get_status();
    assert(hostlink_status.state == ui_hostlink::LinkState::Stopped);
    ui_hostlink::stop();

    const auto heading = orientation::get_heading();
    assert(!heading.available);
    assert(!heading.sensor_ready);
    orientation::ensure_heading_runtime();

    std::vector<std::string> routes;
    assert(!route_storage::is_supported());
    assert(!route_storage::list_routes(routes));
    assert(routes.empty());
    assert(!route_storage::remove_route("/routes/example.kml"));
    assert(std::string(route_storage::route_dir()) == "/routes");

    assert(!sstv::is_supported());
    assert(!sstv::start());
    const auto sstv_status = sstv::get_status();
    assert(sstv_status.state == sstv::State::Idle);
    assert(!sstv::is_active());
    assert(sstv::framebuffer() == nullptr);
    assert(sstv::frame_width() == 0U);
    assert(sstv::frame_height() == 0U);
    sstv::stop();

    std::vector<std::string> tracks;
    std::string current_track;
    assert(!tracker::is_supported());
    assert(!tracker::is_recording());
    assert(!tracker::start_recording());
    assert(!tracker::current_path(current_track));
    assert(current_track.empty());
    assert(!tracker::list_tracks(tracks));
    assert(tracks.empty());
    assert(!tracker::remove_track("/tracks/example.gpx"));
    assert(std::string(tracker::track_dir()) == "/tracks");
    tracker::set_auto_recording(true);
    tracker::set_interval_seconds(5U);
    tracker::set_distance_only(true);
    tracker::set_format(tracker::Format::CSV);
    tracker::stop_recording();

    assert(!usb_support::is_supported());
    assert(!usb_support::start());
    const auto usb_status = usb_support::get_status();
    assert(!usb_status.active);
    assert(usb_status.message != nullptr);
    usb_support::prepare_mass_storage_mode();
    usb_support::restore_mass_storage_mode();
    usb_support::stop();

    assert(!walkie::is_supported());
    assert(!walkie::start());
    assert(!walkie::is_active());
    assert(walkie::volume() == 0);
    const auto walkie_status = walkie::get_status();
    assert(!walkie_status.active);
    assert(walkie::last_error() != nullptr);
    walkie::stop();

    wifi::Config wifi_config{};
    wifi_config.enabled = true;
    std::snprintf(wifi_config.ssid, sizeof(wifi_config.ssid), "%s", "TrailMateNet");
    std::snprintf(wifi_config.password, sizeof(wifi_config.password), "%s", "test-password");
    assert(wifi::save_config(wifi_config));

    wifi::Config wifi_loaded{};
    assert(wifi::load_config(wifi_loaded));
    assert(wifi_loaded.enabled);
    assert(std::string(wifi_loaded.ssid) == "TrailMateNet");
    assert(std::string(wifi_loaded.password) == "test-password");
    assert(!wifi::is_supported());
    assert(!wifi::apply_enabled(true));
    assert(wifi::apply_enabled(false));
    assert(!wifi::connect(&wifi_loaded));
    wifi::disconnect();
    std::vector<wifi::ScanResult> wifi_scan_results;
    assert(!wifi::scan(wifi_scan_results));
    assert(wifi_scan_results.empty());
    const auto wifi_status = wifi::status();
    assert(!wifi_status.supported);
    assert(!wifi_status.connected);
    assert(wifi_status.has_credentials);
    assert(wifi_status.state == wifi::ConnectionState::Unsupported);

    const auto gps_state = ui_gps::get_data();
    assert(!gps_state.valid);
    std::vector<ui_gps::GnssSatInfo> sats(4U);
    ui_gps::GnssStatus gnss_status{};
    std::size_t sat_count = sats.size();
    assert(!ui_gps::get_gnss_snapshot(sats.data(), sats.size(), &sat_count, &gnss_status));
    assert(sat_count == 0U);
    assert(ui_gps::last_motion_ms() == 0U);
    ui_gps::set_collection_interval(1000U);
    ui_gps::set_power_strategy(1U);
    ui_gps::set_gnss_config(1U, 0xFFU);
    ui_gps::set_nmea_config(1U, 0x01U);
    ui_gps::set_motion_idle_timeout(30000U);
    ui_gps::set_motion_sensor_id(2U);
    ui_gps::suspend_runtime();
    ui_gps::resume_runtime();
    const double map_resolution = ui_gps::calculate_map_resolution(0, 0.0);
    assert(std::fabs(map_resolution - 156543.03392) < 0.01);

    assert(!lora::is_supported());
    assert(!lora::acquire());
    assert(!lora::is_online());
    lora::ReceiveConfig receive_config{};
    assert(!lora::configure_receive(433.92f, receive_config));
    assert(std::isnan(lora::read_instant_rssi()));
    lora::release();

    settings_store::clear_namespace("settings");
    settings_store::clear_namespace("runtime_smoke");
    return 0;
}
