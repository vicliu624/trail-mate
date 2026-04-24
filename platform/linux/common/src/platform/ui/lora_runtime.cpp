#include "platform/ui/lora_runtime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <mutex>

namespace platform::ui::lora
{
namespace
{

using Clock = std::chrono::steady_clock;

struct SpectralPeak
{
    float freq_mhz = 0.0f;
    float peak_dbm = -120.0f;
};

constexpr const char* kNoiseFloorEnv = "TRAIL_MATE_LORA_NOISE_FLOOR_DBM";
constexpr const char* kPrimaryPeakEnv = "TRAIL_MATE_LORA_PRIMARY_PEAK_MHZ";
constexpr const char* kSecondaryPeakEnv = "TRAIL_MATE_LORA_SECONDARY_PEAK_MHZ";

constexpr std::array<SpectralPeak, 3> kDefaultPeaks{{
    {433.175f, -69.0f},
    {433.550f, -76.0f},
    {434.225f, -84.0f},
}};

std::mutex s_mutex;
bool s_acquired = false;
bool s_configured = false;
Clock::time_point s_started_at = Clock::now();
float s_freq_mhz = 0.0f;
ReceiveConfig s_config{};

float env_float_or_default(const char* name, float fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return fallback;
    }

    char* end = nullptr;
    const float parsed = std::strtof(value, &end);
    if (end == value || (end && *end != '\0') || !std::isfinite(parsed))
    {
        return fallback;
    }
    return parsed;
}

std::array<SpectralPeak, 3> configured_peaks()
{
    auto peaks = kDefaultPeaks;
    peaks[0].freq_mhz = env_float_or_default(kPrimaryPeakEnv, peaks[0].freq_mhz);
    peaks[1].freq_mhz = env_float_or_default(kSecondaryPeakEnv, peaks[1].freq_mhz);
    return peaks;
}

float gaussian_response(float center, float sample, float sigma)
{
    const float delta = sample - center;
    return std::exp(-(delta * delta) / (2.0f * sigma * sigma));
}

float current_rssi_locked()
{
    if (!s_acquired || !s_configured)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }

    const float noise_floor = env_float_or_default(kNoiseFloorEnv, -119.5f);
    const auto peaks = configured_peaks();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - s_started_at).count();
    const float seconds = static_cast<float>(elapsed_ms) / 1000.0f;

    float rssi = noise_floor +
                 std::sin(seconds * 1.13f + s_freq_mhz * 0.37f) * 2.7f +
                 std::cos(seconds * 0.49f + s_freq_mhz * 1.61f) * 1.5f;

    const float sigma = std::clamp((s_config.bw_khz / 1000.0f) * 0.34f, 0.045f, 0.18f);
    for (std::size_t i = 0; i < peaks.size(); ++i)
    {
        const float response = gaussian_response(peaks[i].freq_mhz, s_freq_mhz, sigma);
        const float peak_dbm = peaks[i].peak_dbm + std::sin(seconds * (0.7f + static_cast<float>(i) * 0.18f)) * 1.8f;
        rssi = std::max(rssi, noise_floor + response * (peak_dbm - noise_floor));
    }

    return std::clamp(rssi, -140.0f, -28.0f);
}

} // namespace

bool is_supported()
{
    return true;
}

bool acquire()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_acquired = true;
    s_started_at = Clock::now();
    return true;
}

bool is_online()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_acquired;
}

bool configure_receive(float freq_mhz, const ReceiveConfig& config)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_acquired || !std::isfinite(freq_mhz) || freq_mhz <= 0.0f)
    {
        return false;
    }
    s_freq_mhz = freq_mhz;
    s_config = config;
    s_configured = true;
    return true;
}

float read_instant_rssi()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return current_rssi_locked();
}

void release()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_acquired = false;
    s_configured = false;
    s_freq_mhz = 0.0f;
}

} // namespace platform::ui::lora
