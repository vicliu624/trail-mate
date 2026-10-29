#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace platform::linux_runtime::detail
{
// Semtech's standard image-calibration bands, matching the RadioLib contract.
// Other frequencies use the documented custom range with odd coefficients.
inline std::array<uint8_t, 2> sx126x_image_calibration(float frequency)
{
    const int band = static_cast<int>(frequency);
    if (band >= 902 && band <= 928) return {0xE1, 0xE9};
    if (band >= 863 && band <= 870) return {0xD7, 0xDB};
    if (band >= 779 && band <= 787) return {0xC1, 0xC5};
    if (band >= 470 && band <= 510) return {0x75, 0x81};
    if (band >= 430 && band <= 440) return {0x6B, 0x6F};
    auto low = static_cast<uint8_t>(std::floor((frequency - 5) / 4));
    auto high = static_cast<uint8_t>(std::ceil((frequency + 5) / 4));
    if (!(low & 1)) --low;
    if (!(high & 1)) ++high;
    return {low, high};
}
} // namespace platform::linux_runtime::detail
