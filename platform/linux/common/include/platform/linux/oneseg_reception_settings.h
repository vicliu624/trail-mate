#pragma once

#include <cstdint>

namespace platform::linux_runtime
{
// Explicit per-generation overrides. Disabled preserves the caller's device profile.
struct OneSegReceptionSettings
{
    bool enabled = false;
    uint32_t sample_rate_hz = 450000;
    uint32_t spi_clock_hz = 8700000;
    unsigned gain_step = 9;

    bool valid() const
    {
        return (sample_rate_hz == 450000 || sample_rate_hz == 500000) && gain_step <= 13 &&
               (spi_clock_hz == 8350000 || spi_clock_hz == 8700000 || spi_clock_hz == 16000000);
    }
    double inputBytesPerSecond() const { return double(sample_rate_hz) * 2; }
    double spiBytesPerSecond() const { return double(spi_clock_hz) / 8; }
    double utilization() const { return inputBytesPerSecond() / spiBytesPerSecond(); }
    double fifoFillUs() const { return 1024.0 * 1000000 / inputBytesPerSecond(); }
    double drainBytesPerSecond() const { return spiBytesPerSecond() - inputBytesPerSecond(); }
};
} // namespace platform::linux_runtime
