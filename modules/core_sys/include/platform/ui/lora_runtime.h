#pragma once

#include <cstdint>

namespace platform::ui::lora
{

struct ReceiveConfig
{
    float bw_khz = 125.0f;
    uint8_t sf = 11;
    uint8_t cr = 5;
    int8_t tx_power = 14;
    uint16_t preamble_len = 8;
    uint8_t sync_word = 0x12;
    uint8_t crc_len = 2;
};

bool is_supported();
bool acquire();
bool is_online();
bool configure_receive(float freq_mhz, const ReceiveConfig& config);
float read_rssi();
void release();

} // namespace platform::ui::lora
