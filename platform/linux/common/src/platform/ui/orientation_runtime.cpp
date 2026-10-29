#include "platform/ui/orientation_runtime.h"
#include "platform/linux/iio_sensors.h"

namespace platform::ui::orientation
{
bool heading_supported() { return false; }
bool acquire_heading() { return false; }
void release_heading() {}

HeadingState get_heading()
{
    return {};
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
