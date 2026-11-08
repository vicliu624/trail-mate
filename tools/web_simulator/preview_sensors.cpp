#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/orientation_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/usb_support_runtime.h"
#include "platform/ui/walkie_runtime.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/ui_common.h"
#include "ui/widgets/top_bar_power_presenter.h"
#include <algorithm>
#include <cstdint>
#include <emscripten.h>

namespace platform::ui::orientation
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
static bool preview_magnetometer = true;
#else
static bool preview_magnetometer = false;
#endif
static unsigned heading_leases = 0;
bool heading_supported() { return preview_magnetometer; }
bool acquire_heading()
{
    if (!heading_supported()) return false;
    ++heading_leases;
    return true;
}
void release_heading()
{
    if (heading_leases) --heading_leases;
}
extern "C" EMSCRIPTEN_KEEPALIVE void native_preview_magnetometer(int supported) { preview_magnetometer = supported != 0; }
extern "C" EMSCRIPTEN_KEEPALIVE unsigned native_preview_heading_leases() { return heading_leases; }
static HeadingState preview_heading{};
HeadingState get_heading() { return preview_heading; }
ScreenOrientationState get_screen_orientation()
{
    ScreenOrientationState state;
    state.sensor_available = preview_magnetometer;
    state.sensor_ready = preview_heading.sensor_ready;
    return state;
}
extern "C" EMSCRIPTEN_KEEPALIVE void native_preview_heading(int degrees, int available)
{
    preview_heading.available = preview_heading.sensor_ready = available != 0;
    preview_heading.heading_deg = static_cast<float>(degrees % 360);
}
} // namespace platform::ui::orientation

namespace platform::ui::device
{
bool gps_supported() { return true; }
bool card_ready() { return true; }
bool sd_ready() { return true; }
int power_tier() { return 0; }
bool gps_ready() { return true; }
void delay_ms(uint32_t) {}
} // namespace platform::ui::device
namespace platform::ui::gps
{
static bool sample_fix = true;
extern "C" EMSCRIPTEN_KEEPALIVE void native_preview_gps_fix(int valid) { sample_fix = valid != 0; }
GpsState get_data()
{
    GpsState s;
    s.lat = 31.2304;
    s.lng = 121.4737;
    s.alt_m = 842;
    s.satellites = 12;
    s.valid = sample_fix;
    s.has_alt = sample_fix;
    return s;
}
bool get_gnss_snapshot(GnssSatInfo* out, std::size_t max, std::size_t* count, GnssStatus* status)
{
    *count = std::min<std::size_t>(12, max);
    for (std::size_t i = 0; i < *count; ++i) out[i] = {static_cast<uint16_t>(i + 1), static_cast<GnssSystem>(i % 4), static_cast<uint16_t>(i * 29), static_cast<uint8_t>(15 + (i * 13) % 70), static_cast<int8_t>(25 + i), sample_fix};
    status->sats_in_use = sample_fix ? 12 : 0;
    status->sats_in_view = 12;
    status->hdop = sample_fix ? .9f : 0;
    status->fix = sample_fix ? GnssFix::FIX3D : GnssFix::NOFIX;
    return true;
}
GpsDiagnosticsSnapshot diagnostics()
{
    GpsDiagnosticsSnapshot s;
    s.enabled = true;
    s.powered = true;
    s.ready = true;
    s.has_fix = sample_fix;
    s.sats_in_view = 12;
    s.sats_in_use = sample_fix ? 12 : 0;
    s.satellites = s.sats_in_use;
    s.last_rx_age_ms = 100;
    s.chars_recent = 512;
    return s;
}
void acquire_power_lease(const char*) {}
void release_power_lease(const char*) {}
} // namespace platform::ui::gps
namespace platform::ui::walkie
{
static Status status{false, false, false, 0, 0, 433.175f};
bool is_supported() { return true; }
bool start()
{
    status.active = true;
    return true;
}
void stop()
{
    status.active = false;
    status.tx = false;
}
bool is_active() { return status.active; }
void set_ptt(bool pressed)
{
    status.tx = pressed && status.active;
    status.tx_level = status.tx ? 65 : 0;
}
bool set_monitor_enabled(bool enabled)
{
    status.monitor_enabled = enabled;
    status.active = enabled;
    return true;
}
bool monitor_enabled() { return status.monitor_enabled; }
int volume() { return 60; }
Status get_status() { return status; }
const char* last_error() { return ""; }
CapabilityStatus capability_status()
{
    return {CapabilityState::Simulated, "Simulated walkie radio session."};
}
} // namespace platform::ui::walkie
namespace platform::ui::usb_support
{
static Status status{};
bool is_supported() { return true; }
bool start()
{
    status = {true, false, "USB Active"};
    return true;
}
void stop() { status = {false, false, "USB Idle"}; }
Status get_status() { return status; }
bool prepare_mass_storage_mode() { return true; }
void restore_mass_storage_mode() {}
} // namespace platform::ui::usb_support
void ui_update_top_bar_battery(ui::widgets::TopBar&) { ui::widgets::top_bar_power::refresh_now(); }
void ui_set_overlay_active(bool) {}
