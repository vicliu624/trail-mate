#include "platform/linux/iio_sensors.h"
#include "platform/ui/orientation_runtime.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>
#include <unistd.h>

namespace
{
void write(const std::filesystem::path& path, const char* value)
{
    std::ofstream output(path.string() + ".tmp");
    output << value;
    output.close();
    std::filesystem::rename(path.string() + ".tmp", path);
}
bool waitFor(const std::function<bool()>& condition)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < until)
    {
        if (condition()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}
} // namespace

int main()
{
    namespace sensors = platform::linux_runtime;
    namespace orientation = platform::ui::orientation;
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-heading-" + std::to_string(getpid()));
    const auto device = root / "iio:device0";
    std::filesystem::create_directories(device);
    write(device / "in_magn_x_raw", "1600");
    write(device / "in_magn_y_raw", "0");
    write(device / "in_magn_z_raw", "1000");
    write(device / "in_magn_scale", "0.000625");
    sensors::MagnetometerSnapshot value;
    assert(sensors::read_iio_magnetometer(device, value));
    assert(value.raw_x == 1600 && std::abs(value.x - 1.0f) < .001f && std::abs(value.heading_deg) < .01f);
    assert(sensors::read_iio_magnetometer(device, value, 90) && std::abs(value.heading_deg - 90) < .01f);
    write(device / "in_magn_x_raw", "0");
    write(device / "in_magn_y_raw", "-1600");
    assert(sensors::read_iio_magnetometer(device, value) && std::abs(value.heading_deg - 90) < .01f);
    write(device / "in_magn_y_raw", "1600");
    assert(sensors::read_iio_magnetometer(device, value) && std::abs(value.heading_deg - 270) < .01f);
    write(device / "in_magn_y_raw", "0");
    assert(!sensors::read_iio_magnetometer(device, value) && !value.ready);
    write(device / "in_magn_x_raw", "NaN");
    assert(!sensors::read_iio_magnetometer(device, value));
    write(device / "in_magn_x_raw", "1600");
    sensors::IioSensorOptions options;
    options.root = root;
    options.expected = true;
    assert(sensors::configure_iio_sensors(options));
    assert(waitFor([]
                   { return orientation::get_heading().available; }));
    assert(orientation::heading_supported() && orientation::acquire_heading());
    assert(!sensors::linux_imu_snapshot().ready); // Heading does not depend on screen rotation or an accelerometer.
    assert(orientation::get_heading().raw_x == 1600);
    std::filesystem::remove(device / "in_magn_z_raw");
    assert(waitFor([]
                   { return !orientation::get_heading().available; }));
    assert(orientation::heading_supported()); // Stable hardware capability survives a transient read failure.
    std::filesystem::remove_all(device);
    assert(waitFor([]
                   { return !orientation::heading_supported(); }));
    orientation::release_heading();
    sensors::shutdown_iio_sensors();
    assert(!orientation::get_heading().available);
    std::filesystem::remove_all(root);
}
