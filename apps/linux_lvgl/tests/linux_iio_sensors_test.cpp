#include "platform/linux/iio_sensors.h"
#include "platform/ui/orientation_runtime.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>
#include <unistd.h>

namespace
{
void write(const std::filesystem::path& path, const char* value)
{
    {
        std::ofstream output(path.string() + ".tmp");
        output << value;
    }
    std::filesystem::rename(path.string() + ".tmp", path);
}
bool waitFor(const std::function<bool()>& condition)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do
    {
        if (condition()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } while (std::chrono::steady_clock::now() < end);
    return false;
}
} // namespace

int main()
{
    using namespace platform::linux_runtime;
    if (gravity_rotation(0, 9.8f, 0) != 0 || gravity_rotation(0, -9.8f, 0) != 180 ||
        gravity_rotation(9.8f, 0, 0) != 270 || gravity_rotation(-9.8f, 0, 0) != 90 ||
        gravity_rotation(0, 0, 180) != 180 || gravity_rotation(0, 9.8f, 0, 90) != 90) return 1;
    char name[] = "/tmp/trailmate-iio-test-XXXXXX";
    if (!mkdtemp(name)) return 2;
    const std::filesystem::path root(name), device = root / "iio:device0";
    std::filesystem::create_directory(device);
    write(device / "in_accel_x_raw", "0");
    write(device / "in_accel_y_raw", "4096");
    write(device / "in_accel_z_raw", "0");
    write(device / "in_accel_scale", "0.002394201");
    write(device / "in_anglvel_x_raw", "0");
    write(device / "in_anglvel_y_raw", "0");
    write(device / "in_anglvel_z_raw", "0");
    write(device / "in_anglvel_scale", "0.000272707");
    write(device / "in_temp_raw", "6400");
    write(device / "in_temp_scale", "3.90625");
    ImuSnapshot reading;
    if (!read_iio_imu(device, reading) || reading.accel_y < 9.7f || reading.accel_y > 9.9f ||
        !reading.temperature_available || reading.temperature_c != 25.0f || !reading.gyro_available) return 3;
    IioSensorOptions options;
    options.root = root;
    options.expected = true;
    options.landscape_rotation = true;
    int result = 0;
    {
        IioSensorRuntime runtime(options);
        if (!waitFor([&]
                     { return runtime.snapshot().ready; })) result = 4;
        write(device / "in_accel_y_raw", "-4096");
        if (!waitFor([&]
                     { return runtime.snapshot().rotation == 180; })) result = 5;
        write(device / "in_accel_y_raw", "0");
        write(device / "in_accel_x_raw", "4096");
        if (!waitFor([&]
                     { return runtime.snapshot().portrait_requested; })) result = 6;
        const auto state = runtime.snapshot();
        if (state.rotation != 180 || !state.last_motion_ms) result = 7;
        std::filesystem::remove(device / "in_accel_y_raw");
        if (!waitFor([&]
                     { return !runtime.snapshot().ready; })) result = 8;
        if (!runtime.snapshot().expected) result = 9;
    }
    write(device / "in_accel_y_raw", "0");
    if (!configure_iio_sensors(options) || !waitFor([]
                                                    { return linux_imu_snapshot().ready; })) result = 10;
    const auto orientation = platform::ui::orientation::get_screen_orientation();
    if (!orientation.sensor_available || !orientation.sensor_ready ||
        orientation.policy != platform::ui::orientation::ScreenOrientationPolicy::SensorLandscapeOnly ||
        platform::ui::orientation::heading_supported()) result = 11;
    shutdown_iio_sensors();
    std::filesystem::remove_all(root);
    return result;
}
