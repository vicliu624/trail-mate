#include "platform/linux/iio_sensors.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>

namespace platform::linux_runtime
{
namespace
{
bool number(const std::filesystem::path& path, double& value)
{
    std::ifstream file(path);
    if (!(file >> value) || !std::isfinite(value)) return false;
    file >> std::ws;
    return file.eof();
}
bool channels(const std::filesystem::path& path, const char* type, float& x, float& y, float& z)
{
    float* output[] = {&x, &y, &z};
    const char* axes[] = {"x", "y", "z"};
    for (unsigned index = 0; index < 3; ++index)
    {
        const auto base = std::string("in_") + type;
        const auto channel = base + "_" + axes[index];
        double raw = 0, scale = 0, offset = 0;
        if (!number(path / (channel + "_raw"), raw)) return false;
        if (!number(path / (channel + "_scale"), scale) && !number(path / (base + "_scale"), scale)) return false;
        if (!number(path / (channel + "_offset"), offset)) number(path / (base + "_offset"), offset);
        const auto physical = (raw + offset) * scale;
        if (!std::isfinite(physical) || scale <= 0) return false;
        *output[index] = static_cast<float>(physical);
    }
    return true;
}
uint32_t milliseconds()
{
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}
} // namespace

bool read_iio_imu(const std::filesystem::path& device, ImuSnapshot& output)
{
    output.ready = false;
    if (!channels(device, "accel", output.accel_x, output.accel_y, output.accel_z)) return false;
    output.gyro_available = channels(device, "anglvel", output.gyro_x, output.gyro_y, output.gyro_z);
    double raw = 0, scale = 0, offset = 0;
    output.temperature_available = number(device / "in_temp_raw", raw) && number(device / "in_temp_scale", scale);
    if (output.temperature_available)
    {
        number(device / "in_temp_offset", offset);
        output.temperature_c = static_cast<float>((raw + offset) * scale / 1000.0);
    }
    else if (number(device / "in_temp_input", raw))
    {
        output.temperature_available = true;
        output.temperature_c = static_cast<float>(raw / 1000.0);
    }
    output.ready = true;
    output.updated_ms = milliseconds();
    return true;
}

unsigned gravity_rotation(float x, float y, unsigned previous, int mount_rotation)
{
    // Avoid unstable decisions while flat or between two cardinal directions.
    if (!std::isfinite(x) || !std::isfinite(y) || std::max(std::abs(x), std::abs(y)) < 4.0f ||
        std::abs(std::abs(x) - std::abs(y)) < 1.5f) return previous;
    const unsigned raw = std::abs(y) > std::abs(x) ? (y >= 0 ? 0 : 180) : (x >= 0 ? 270 : 90);
    return static_cast<unsigned>((static_cast<int>(raw) + mount_rotation % 360 + 360) % 360);
}

struct IioSensorRuntime::Impl
{
    explicit Impl(IioSensorOptions value) : options(std::move(value))
    {
        data.expected = options.expected;
        data.rotation_enabled = options.landscape_rotation;
        worker = std::thread([this]
                             { run(); });
    }
    ~Impl()
    {
        stopping = true;
        changed.notify_all();
        worker.join();
    }
    std::filesystem::path discover()
    {
        if (!options.device.empty()) return options.device;
        std::error_code error;
        auto iterator = std::filesystem::directory_iterator(options.root, error);
        while (!error && iterator != std::filesystem::directory_iterator{})
        {
            const auto path = iterator->path();
            if (std::filesystem::exists(path / "in_accel_x_raw", error) &&
                std::filesystem::exists(path / "in_accel_y_raw", error) &&
                std::filesystem::exists(path / "in_accel_z_raw", error)) return path;
            iterator.increment(error);
        }
        return {};
    }
    void run()
    {
        std::filesystem::path device;
        ImuSnapshot previous = data;
        unsigned candidate = 0, consecutive = 0;
        while (!stopping)
        {
            if (device.empty()) device = discover();
            ImuSnapshot sample;
            sample.expected = options.expected;
            sample.rotation_enabled = options.landscape_rotation;
            sample.rotation = previous.rotation;
            sample.last_motion_ms = previous.last_motion_ms;
            std::error_code error;
            sample.present = !device.empty() && std::filesystem::exists(device, error);
            if (sample.present && read_iio_imu(device, sample))
            {
                const unsigned desired = gravity_rotation(sample.accel_x, sample.accel_y, candidate, options.mount_rotation);
                if (desired == candidate) ++consecutive;
                else
                {
                    candidate = desired;
                    consecutive = 1;
                }
                sample.portrait_requested = candidate == 90 || candidate == 270;
                if (options.landscape_rotation && !sample.portrait_requested && consecutive >= 3)
                    sample.rotation = candidate;
                const float acceleration_delta = std::abs(sample.accel_x - previous.accel_x) +
                                                 std::abs(sample.accel_y - previous.accel_y) + std::abs(sample.accel_z - previous.accel_z);
                const float angular_rate = std::max({std::abs(sample.gyro_x), std::abs(sample.gyro_y), std::abs(sample.gyro_z)});
                if (previous.ready && (acceleration_delta > 1.2f || (sample.gyro_available && angular_rate > 0.1f)))
                    sample.last_motion_ms = sample.updated_ms;
            }
            else device.clear();
            {
                std::lock_guard<std::mutex> lock(mutex);
                data = sample;
            }
            previous = sample;
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait_for(lock, std::chrono::milliseconds(sample.ready ? 100 : 2000), [&]
                             { return stopping.load(); });
        }
    }
    IioSensorOptions options;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::atomic_bool stopping{false};
    ImuSnapshot data;
    std::thread worker;
};

IioSensorRuntime::IioSensorRuntime(IioSensorOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
IioSensorRuntime::~IioSensorRuntime() = default;
ImuSnapshot IioSensorRuntime::snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->data;
}

namespace
{
struct Runtime
{
    std::mutex mutex;
    std::unique_ptr<IioSensorRuntime> sensor;
};
Runtime& runtime()
{
    static Runtime value;
    return value;
}
} // namespace
bool configure_iio_sensors(const IioSensorOptions& options)
{
    if (options.mount_rotation < 0 || options.mount_rotation >= 360 || options.mount_rotation % 90) return false;
    shutdown_iio_sensors();
    if (!options.expected && options.device.empty() && !options.landscape_rotation) return true;
    auto sensor = std::make_unique<IioSensorRuntime>(options);
    auto& state = runtime();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.sensor = std::move(sensor);
    return true;
}
ImuSnapshot linux_imu_snapshot()
{
    auto& state = runtime();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.sensor ? state.sensor->snapshot() : ImuSnapshot{};
}
void shutdown_iio_sensors()
{
    std::unique_ptr<IioSensorRuntime> retiring;
    auto& state = runtime();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        retiring = std::move(state.sensor);
    }
}
} // namespace platform::linux_runtime
