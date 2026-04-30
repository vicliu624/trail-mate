#include "app/linux_app_facade.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/ui/team_ui_store_runtime.h"
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
#include "team/protocol/team_track.h"
#include "team/usecase/team_pairing_service.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

void set_env_var(const char* name, const std::string& value)
{
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

} // namespace

int main()
{
    using namespace platform::ui;
    namespace ui_gps = platform::ui::gps;
    namespace ui_hostlink = platform::ui::hostlink;
    using trailmate::cardputer_zero::linux_ui::MinimalLinuxAppFacade;

    const std::filesystem::path runtime_root =
        std::filesystem::temp_directory_path() / "trailmate-linux-runtime-smoke";
    std::error_code ec;
    std::filesystem::remove_all(runtime_root, ec);
    std::filesystem::create_directories(runtime_root, ec);
    assert(!ec);

    set_env_var("TRAIL_MATE_SETTINGS_ROOT", runtime_root.string());
    set_env_var("TRAIL_MATE_HOSTLINK_PORT", "44192");
    set_env_var("TRAIL_MATE_GPS_SUPPORTED", "1");
    set_env_var("TRAIL_MATE_GPS_READY", "1");

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
    assert(device::gps_supported());
    assert(device::gps_ready());
    device::delay_ms(1);

    assert(!firmware_update::is_supported());
    const auto firmware_status = firmware_update::status();
    assert(!firmware_status.supported);
    assert(firmware_status.phase == firmware_update::Phase::Unsupported);
    assert(!firmware_update::start_check());
    assert(!firmware_update::start_install());

    assert(ui_hostlink::is_supported());
    ui_hostlink::start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(ui_hostlink::is_active());
    const auto hostlink_status = ui_hostlink::get_status();
    assert(hostlink_status.state == ui_hostlink::LinkState::Waiting ||
           hostlink_status.state == ui_hostlink::LinkState::Error);
    ui_hostlink::stop();
    assert(!ui_hostlink::is_active());
    assert(ui_hostlink::get_status().state == ui_hostlink::LinkState::Stopped);

    const auto heading = orientation::get_heading();
    assert(!heading.available);
    assert(!heading.sensor_ready);
    orientation::ensure_heading_runtime();

    std::vector<std::string> routes;
    assert(route_storage::is_supported());
    assert(route_storage::list_routes(routes));
    assert(routes.empty());
    assert(!route_storage::remove_route("/routes/example.kml"));
    const std::filesystem::path route_dir_path(route_storage::route_dir());
    assert(!route_dir_path.empty());
    std::filesystem::create_directories(route_dir_path, ec);
    assert(std::filesystem::is_directory(route_dir_path));

    assert(sstv::is_supported());
    assert(sstv::framebuffer() != nullptr);
    assert(sstv::frame_width() > 0U);
    assert(sstv::frame_height() > 0U);
    assert(sstv::start());
    assert(sstv::is_active());
    const auto sstv_status = sstv::get_status();
    assert(sstv_status.state == sstv::State::Waiting || sstv_status.state == sstv::State::Receiving);
    sstv::stop();
    assert(!sstv::is_active());

    std::vector<std::string> tracks;
    std::string current_track;
    assert(tracker::is_supported());
    assert(!tracker::is_recording());
    assert(tracker::start_recording());
    assert(tracker::is_recording());
    assert(tracker::current_path(current_track));
    assert(!current_track.empty());
    tracker::stop_recording();
    assert(!tracker::is_recording());
    assert(tracker::list_tracks(tracks));
    assert(!tracks.empty());
    assert(!tracker::remove_track("/tracks/example.gpx"));
    const std::filesystem::path track_dir_path(tracker::track_dir());
    assert(!track_dir_path.empty());
    std::filesystem::create_directories(track_dir_path, ec);
    assert(std::filesystem::is_directory(track_dir_path));
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

    assert(walkie::is_supported());
    assert(walkie::start());
    assert(walkie::is_active());
    assert(walkie::volume() > 0);
    const auto walkie_status = walkie::get_status();
    assert(walkie_status.active);
    assert(walkie_status.freq_mhz > 0.0f);
    assert(walkie::last_error() != nullptr);
    walkie::stop();
    assert(!walkie::is_active());

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
    assert(gps_state.valid);
    assert(gps_state.has_alt);
    assert(gps_state.has_speed);
    assert(gps_state.has_course);
    std::vector<ui_gps::GnssSatInfo> sats(4U);
    ui_gps::GnssStatus gnss_status{};
    std::size_t sat_count = sats.size();
    assert(ui_gps::get_gnss_snapshot(sats.data(), sats.size(), &sat_count, &gnss_status));
    assert(sat_count == sats.size());
    assert(gnss_status.fix == ui_gps::GnssFix::FIX3D);
    assert(gnss_status.sats_in_use > 0U);
    assert(ui_gps::last_motion_ms() > 0U);
    assert(ui_gps::is_enabled());
    assert(ui_gps::is_powered());
    ui_gps::set_collection_interval(1000U);
    ui_gps::set_power_strategy(1U);
    ui_gps::set_gnss_config(1U, 0xFFU);
    ui_gps::set_external_nmea_config(1U, 0x01U);
    ui_gps::set_motion_idle_timeout(30000U);
    ui_gps::set_motion_sensor_id(2U);
    ui_gps::set_enabled(false);
    assert(!ui_gps::is_enabled());
    ui_gps::set_enabled(true);
    assert(ui_gps::is_enabled());
    ui_gps::suspend_runtime();
    assert(!ui_gps::is_enabled());
    assert(!ui_gps::is_powered());
    ui_gps::resume_runtime();
    assert(ui_gps::is_enabled());
    assert(ui_gps::is_powered());
    const double map_resolution = ui_gps::calculate_map_resolution(0, 0.0);
    assert(std::fabs(map_resolution - 156543.03392) < 0.01);

    const std::filesystem::path nmea_file = runtime_root / "gps_feed.nmea";
    {
        std::ofstream nmea_stream(nmea_file, std::ios::trunc);
        nmea_stream << "$GNRMC,123519,A,3113.8240,N,12128.4220,E,1.20,84.4,230394,,,\n";
        nmea_stream << "$GNGGA,123520,3113.8240,N,12128.4220,E,1,10,0.9,15.2,M,0.0,M,,\n";
        nmea_stream << "$GNGSA,A,3,03,08,14,22,07,19,31,,,,,0.0,0.9,1.2\n";
        nmea_stream << "$GPGSV,1,1,4,03,74,018,42,08,58,084,39,14,41,156,35,22,49,286,31\n";
    }
    set_env_var("TRAIL_MATE_GPS_NMEA_FILE", nmea_file.string());

    const auto gps_nmea_state = ui_gps::get_data();
    assert(gps_nmea_state.valid);
    assert(std::fabs(gps_nmea_state.lat - 31.2304) < 0.0002);
    assert(std::fabs(gps_nmea_state.lng - 121.4737) < 0.0002);
    assert(std::fabs(gps_nmea_state.alt_m - 15.2) < 0.01);
    assert(gps_nmea_state.satellites == 10U);

    std::vector<ui_gps::GnssSatInfo> nmea_sats(8U);
    ui_gps::GnssStatus nmea_status{};
    std::size_t nmea_sat_count = nmea_sats.size();
    assert(ui_gps::get_gnss_snapshot(nmea_sats.data(), nmea_sats.size(), &nmea_sat_count, &nmea_status));
    assert(nmea_status.fix == ui_gps::GnssFix::FIX3D);
    assert(nmea_status.sats_in_use >= 7U);
    assert(nmea_sat_count >= 4U);

    assert(lora::is_supported());
    assert(lora::acquire());
    assert(lora::is_online());
    lora::ReceiveConfig receive_config{};
    assert(lora::configure_receive(433.92f, receive_config));
    assert(std::isfinite(lora::read_instant_rssi()));
    lora::release();
    assert(!lora::is_online());

    MinimalLinuxAppFacade facade;
    assert(facade.initialize());
    facade.dispatchPendingEvents();

    const auto contacts = facade.getContactService().getContacts();
    const auto nearby = facade.getContactService().getNearby();
    const auto ignored = facade.getContactService().getIgnoredNodes();
    assert(contacts.size() >= 2U);
    assert(!ignored.empty());
    assert((contacts.size() + nearby.size() + ignored.size()) >= 4U);

    std::size_t total_conversations = 0;
    const auto conversations = facade.getChatService().getConversations(0, 8, &total_conversations);
    assert(total_conversations >= 3U);
    assert(!conversations.empty());

    auto* pairing = facade.getTeamPairing();
    assert(pairing != nullptr);
    assert(pairing->startMember(facade.getSelfNodeId()));
    for (int i = 0; i < 16; ++i)
    {
        facade.tickEventRuntime();
        facade.dispatchPendingEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }

    team::ui::TeamUiSnapshot team_snapshot{};
    assert(team::ui::team_ui_get_store().load(team_snapshot));
    assert(team_snapshot.in_team);
    assert(team_snapshot.has_team_id);
    assert(team_snapshot.has_team_psk);
    assert(team_snapshot.members.size() >= 2U);

    team::proto::TeamTrackMessage track{};
    track.start_ts = static_cast<uint32_t>(std::time(nullptr));
    track.interval_s = 10;
    track.valid_mask = 0x3U;
    track.points.push_back({311234567, 1214567890});
    track.points.push_back({311235000, 1214568200});
    assert(team::ui::team_ui_append_member_track(team_snapshot.team_id, 0x435A1001U, track));
    std::string member_track_path;
    assert(team::ui::team_ui_get_member_track_path(team_snapshot.team_id, 0x435A1001U, member_track_path));
    assert(!member_track_path.empty());
    assert(std::filesystem::exists(member_track_path));
    facade.shutdown();

    settings_store::clear_namespace("settings");
    settings_store::clear_namespace("runtime_smoke");
    std::filesystem::remove_all(runtime_root, ec);
    return 0;
}
