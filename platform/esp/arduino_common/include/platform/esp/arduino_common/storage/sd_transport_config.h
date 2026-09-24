#pragma once

#include <cstdint>

namespace platform::esp::arduino_common::storage
{
// Hardware facts only. Buffer sizing and transfer algorithms belong to the
// shared transport implementation, not to individual board profiles.
struct SdmmcSdConfig
{
    int clock = -1;
    int command = -1;
    int data0 = -1;
    int data1 = -1;
    int data2 = -1;
    int data3 = -1;
    int slot = 1;
    uint8_t width = 1;
    uint32_t max_frequency_khz = 20000;
    bool internal_pullups = true;
};
} // namespace platform::esp::arduino_common::storage
