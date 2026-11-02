#pragma once
#include "platform/linux/oneseg_ofdm_lab.h"

namespace platform::linux_runtime
{
struct OneSegSignalMeasurement
{
    // Spectrum: Hz/dBFS per displayed FFT bin; constellation: I/Q;
    // channel: carrier index/dB; CP: offset/rho; kind5: carrier/unwrapped rad.
    unsigned kind = 0;
    uint64_t sequence = 0, updated_ms = 0;
    OneSegLabSource source = OneSegLabSource::Unavailable;
    double sample_rate = 0, metric = 0;
    size_t count = 0;
    struct Point
    {
        double x = 0, y = 0;
    };
    std::array<Point, 64> points{};
    bool valid() const
    {
        if (kind < 1 || kind > 5 || !sequence || count < 1 || count > points.size() ||
            !std::isfinite(sample_rate) || sample_rate <= 0 || sample_rate > 2000000 ||
            !std::isfinite(metric) || metric < 0 || metric > 1e9) return false;
        for (size_t i = 0; i < count; ++i)
        {
            const auto& p = points[i];
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e9 || std::abs(p.y) > 1e9) return false;
            if (kind != 2 && i && p.x <= points[i - 1].x) return false;
            if (kind == 4 && (p.y < 0 || p.y > 1)) return false;
        }
        return true;
    }
    bool fresh(uint64_t now) const { return count && now >= updated_ms && now - updated_ms <= 1500; }
};
} // namespace platform::linux_runtime
