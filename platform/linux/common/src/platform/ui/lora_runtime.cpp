#include "platform/ui/lora_runtime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>

#include "platform/linux/capability_status.h"
#include "platform/linux/packet_radio.h"
#include "platform/linux/runtime_mode.h"

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
constexpr const char* kSimulatedLoraEnv = "TRAIL_MATE_LORA_SIMULATED";

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
bool s_real_driver = false;
std::string s_capability_message{};
::platform::linux_runtime::Sx126xPacket s_packet_scratch{};

bool env_flag_or_default(const char* name, bool fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return fallback;
    }

    return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "TRUE") == 0 || std::strcmp(value, "yes") == 0 ||
           std::strcmp(value, "YES") == 0;
}

bool simulated_lora_enabled()
{
    if (const char* value = std::getenv(kSimulatedLoraEnv);
        value != nullptr && value[0] != '\0')
    {
        return env_flag_or_default(kSimulatedLoraEnv, false);
    }
    return ::platform::linux_runtime::resolve_runtime_mode() ==
           ::platform::linux_runtime::LinuxRuntimeMode::SimulatorDemo;
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
    return ::platform::linux_runtime::mesh_packet_radio().hardwareCandidatePresent() ||
           simulated_lora_enabled();
}

CapabilityStatus capability_status()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    auto& radio = ::platform::linux_runtime::mesh_packet_radio();
    if (radio.isOnline())
    {
        s_capability_message = std::string(radio.name()) + " is bound through Linux spidev/gpiochip.";
        return {CapabilityState::Available, s_capability_message.c_str()};
    }
    if (radio.hardwareCandidatePresent())
    {
        const char* last_error = radio.lastError();
        s_capability_message = std::string(radio.name()) + " endpoint present; driver not online";
        if (last_error != nullptr && last_error[0] != '\0')
        {
            s_capability_message += ": ";
            s_capability_message += last_error;
        }
        else
        {
            s_capability_message += ".";
        }
        return {CapabilityState::Degraded, s_capability_message.c_str()};
    }
    if (simulated_lora_enabled())
    {
        return {CapabilityState::Simulated,
                "Synthetic RSSI generator. No real LoRa radio driver."};
    }
    return {CapabilityState::Unsupported,
            "No LoRa SPI endpoint is present."};
}

bool acquire()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_acquired) return true;
    auto& radio = ::platform::linux_runtime::foreground_packet_radio();
    s_real_driver = radio.acquire();
    s_acquired = s_real_driver || simulated_lora_enabled();
    s_started_at = Clock::now();
    return s_acquired;
}

bool is_online()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_acquired;
}

bool configure_receive(float freq_mhz, const ReceiveConfig& config)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    // A rejected retune must not admit packets or RSSI from the old profile.
    s_configured = false;
    if (!s_acquired || !std::isfinite(freq_mhz) || freq_mhz <= 0.0f)
    {
        return false;
    }
    if (s_real_driver)
    {
        auto lora_config =
            ::platform::linux_runtime::Sx126xRadio::
                defaultLoRaConfigFromEnvironment();
        lora_config.freq_mhz = freq_mhz;
        lora_config.bw_khz = config.bw_khz;
        lora_config.sf = config.sf;
        lora_config.cr = config.cr;
        lora_config.tx_power_dbm = config.tx_power;
        lora_config.preamble_len = config.preamble_len;
        lora_config.sync_word = config.sync_word;
        lora_config.crc_len = config.crc_len;
        if (!::platform::linux_runtime::foreground_packet_radio().configureLoRa(lora_config))
            return false;
    }
    else if (!simulated_lora_enabled()) return false;
    s_freq_mhz = freq_mhz;
    s_config = config;
    s_configured = true;
    return true;
}

bool transmit_packet(const uint8_t* data, std::size_t size)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_acquired || !s_configured || !s_real_driver || !data || size == 0)
    {
        return false;
    }

    auto& radio = ::platform::linux_runtime::foreground_packet_radio();
    const bool sent = radio.transmit(data, size);
    (void)radio.startReceive();
    return sent;
}

float read_instant_rssi()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_acquired || !s_configured) return std::numeric_limits<float>::quiet_NaN();
    if (s_real_driver)
    {
        return ::platform::linux_runtime::foreground_packet_radio().readRssi();
    }
    return current_rssi_locked();
}

bool poll_received_packet(uint8_t* buffer, std::size_t capacity, ReceivedPacket* out_packet)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_acquired || !s_configured || !s_real_driver || !buffer || capacity == 0 ||
        !out_packet)
    {
        return false;
    }

    if (!::platform::linux_runtime::foreground_packet_radio().pollReceive(&s_packet_scratch) ||
        s_packet_scratch.size == 0 || s_packet_scratch.size > capacity)
    {
        return false;
    }

    std::memcpy(buffer, s_packet_scratch.data.data(), s_packet_scratch.size);
    out_packet->size = s_packet_scratch.size;
    out_packet->rssi_dbm = s_packet_scratch.rssi_dbm;
    out_packet->snr_db = s_packet_scratch.snr_db;
    return true;
}

void release()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_acquired = false;
    s_configured = false;
    if (s_real_driver)
    {
        ::platform::linux_runtime::foreground_packet_radio().release();
    }
    s_real_driver = false;
    s_freq_mhz = 0.0f;
}

} // namespace platform::ui::lora
