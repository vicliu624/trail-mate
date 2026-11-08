#include "platform/ui/orientation_runtime.h"
#include "platform/linux/iio_sensors.h"

namespace platform::ui::orientation
{
bool heading_supported() { return ::platform::linux_runtime::linux_magnetometer_snapshot().present; }
bool acquire_heading() { return heading_supported(); }
void release_heading() {}

HeadingState get_heading()
{
    const auto magnetic = ::platform::linux_runtime::linux_magnetometer_snapshot();
    HeadingState state;
    state.sensor_ready = magnetic.ready;
    state.available = magnetic.ready;
    state.heading_deg = magnetic.heading_deg;
    state.raw_x = magnetic.raw_x;
    state.raw_y = magnetic.raw_y;
    state.raw_z = magnetic.raw_z;
    state.last_update_ms = magnetic.updated_ms;
    return state;
}

void ensure_heading_runtime()
{
}

ScreenOrientationState get_screen_orientation()
{
    const auto imu = ::platform::linux_runtime::linux_imu_snapshot();
    ScreenOrientationState state;
    state.policy = imu.rotation_enabled ? ScreenOrientationPolicy::SensorLandscapeOnly : ScreenOrientationPolicy::LandscapeLocked;
    state.sensor_available = imu.expected || imu.present;
    state.sensor_ready = imu.ready;
    state.sensor_requested_orientation = imu.portrait_requested ? ScreenOrientation::Portrait : ScreenOrientation::Landscape;
    state.sensor_request_ignored = imu.portrait_requested;
    return state;
}

void ensure_screen_orientation_runtime()
{
}

} // namespace platform::ui::orientation
