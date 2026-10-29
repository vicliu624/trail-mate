#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

namespace platform::linux_runtime
{
struct IioSensorOptions
{
    std::filesystem::path root = "/sys/bus/iio/devices";
    std::filesystem::path device;
    bool expected = false;
    bool landscape_rotation = false;
    int mount_rotation = 0;
};
struct ImuSnapshot
{
    bool expected = false, present = false, ready = false;
    bool temperature_available = false, gyro_available = false;
    bool rotation_enabled = false, portrait_requested = false;
    float accel_x = 0, accel_y = 0, accel_z = 0;
    float gyro_x = 0, gyro_y = 0, gyro_z = 0, temperature_c = 0;
    unsigned rotation = 0;
    uint32_t updated_ms = 0, last_motion_ms = 0;
};

// Standard IIO channel names/scales; no sensor-model or board identification.
bool read_iio_imu(const std::filesystem::path& device, ImuSnapshot& output);
unsigned gravity_rotation(float x, float y, unsigned previous, int mount_rotation = 0);

class IioSensorRuntime
{
  public:
    explicit IioSensorRuntime(IioSensorOptions options);
    ~IioSensorRuntime();
    ImuSnapshot snapshot() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

bool configure_iio_sensors(const IioSensorOptions& options);
ImuSnapshot linux_imu_snapshot();
void shutdown_iio_sensors();
} // namespace platform::linux_runtime
