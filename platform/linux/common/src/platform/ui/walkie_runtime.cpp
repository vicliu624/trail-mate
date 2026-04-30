#include "platform/ui/walkie_runtime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace platform::ui::walkie
{
namespace
{

using Clock = std::chrono::steady_clock;

constexpr const char* kFreqEnv = "TRAIL_MATE_WALKIE_FREQ_MHZ";
constexpr const char* kVolumeEnv = "TRAIL_MATE_WALKIE_VOLUME";
constexpr const char* kForceTxEnv = "TRAIL_MATE_WALKIE_FORCE_TX";

std::mutex s_mutex;
bool s_active = false;
Clock::time_point s_started_at = Clock::now();
std::string s_last_error{};

bool env_flag_enabled(const char* name)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return false;
    }
    return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "TRUE") == 0 || std::strcmp(value, "yes") == 0 ||
           std::strcmp(value, "YES") == 0;
}

int env_int_or_default(const char* name, int fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return fallback;
    }

    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || (end && *end != '\0'))
    {
        return fallback;
    }
    return static_cast<int>(parsed);
}

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

uint8_t synth_level(float phase, float floor_level, float amplitude)
{
    const float wave = std::sin(phase);
    const float value = floor_level + ((wave + 1.0f) * 0.5f * amplitude);
    const float clamped = std::clamp(value, 0.0f, 100.0f);
    return static_cast<uint8_t>(std::lround(clamped));
}

Status current_status_locked()
{
    Status status{};
    status.active = s_active;
    status.freq_mhz = env_float_or_default(kFreqEnv, 433.500f);
    if (!s_active)
    {
        return status;
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - s_started_at).count();
    const float seconds = static_cast<float>(elapsed) / 1000.0f;
    const bool force_tx = env_flag_enabled(kForceTxEnv);

    status.tx = force_tx;
    if (force_tx)
    {
        status.tx_level = synth_level(seconds * 7.4f, 30.0f, 62.0f);
        status.rx_level = synth_level(seconds * 5.1f, 2.0f, 8.0f);
    }
    else
    {
        status.tx_level = synth_level(seconds * 9.3f, 0.0f, 5.0f);
        status.rx_level = synth_level(seconds * 4.8f, 24.0f, 58.0f);
    }
    return status;
}

} // namespace

bool is_supported()
{
    return true;
}

bool start()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_last_error.clear();
    s_active = true;
    s_started_at = Clock::now();
    return true;
}

void stop()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_active = false;
}

bool is_active()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_active;
}

int volume()
{
    return std::clamp(env_int_or_default(kVolumeEnv, 78), 0, 100);
}

Status get_status()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return current_status_locked();
}

const char* last_error()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_last_error.empty() ? "" : s_last_error.c_str();
}

} // namespace platform::ui::walkie
