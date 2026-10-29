#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace platform::linux_runtime
{
struct FskSettings
{
    float freq_mhz = 0;
    float bitrate_kbps = 9.6f, deviation_khz = 5.0f, rx_bandwidth_khz = 156.2f;
    int8_t tx_power_dbm = 0;
    uint16_t preamble_bits = 16;
    std::array<uint8_t, 8> sync_word{0x2D, 0x01};
    uint8_t sync_word_size = 2;
};
inline bool valid_fsk_settings(const FskSettings& value)
{
    return std::isfinite(value.freq_mhz) && value.freq_mhz >= 150 && value.freq_mhz <= 2500 &&
           std::isfinite(value.bitrate_kbps) && value.bitrate_kbps >= 0.5f && value.bitrate_kbps <= 2000 &&
           std::isfinite(value.deviation_khz) && value.deviation_khz > 0 && value.deviation_khz <= 500 &&
           std::isfinite(value.rx_bandwidth_khz) && value.rx_bandwidth_khz > 0 && value.rx_bandwidth_khz <= 3076 &&
           value.tx_power_dbm >= -19 && value.tx_power_dbm <= 22 && value.preamble_bits >= 8 &&
           value.sync_word_size > 0 && value.sync_word_size <= value.sync_word.size();
}
inline bool valid_sx126x_fsk_settings(const FskSettings& value)
{
    return valid_fsk_settings(value) && value.freq_mhz <= 960 && value.bitrate_kbps >= 1 && value.bitrate_kbps <= 300 &&
           value.deviation_khz <= 200 && value.rx_bandwidth_khz <= 467 && value.tx_power_dbm >= -9 && value.preamble_bits >= 16;
}
inline bool valid_lr2021_fsk_settings(const FskSettings& value)
{
    if (!valid_fsk_settings(value) || value.deviation_khz < 0.6f || value.rx_bandwidth_khz < 4.8f) return false;
    if (value.freq_mhz <= 1090) return value.tx_power_dbm >= -9;
    return ((value.freq_mhz >= 1900 && value.freq_mhz <= 2200) || value.freq_mhz >= 2400) && value.tx_power_dbm <= 12;
}
} // namespace platform::linux_runtime
