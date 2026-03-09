#pragma once

#include <cstdint>

namespace platform::ui::walkie
{

struct Status
{
    bool active = false;
    bool tx = false;
    uint8_t tx_level = 0;
    uint8_t rx_level = 0;
    float freq_mhz = 0.0f;
};

bool is_supported();
bool start();
void stop();
bool is_active();
int volume();
Status get_status();
const char* last_error();

} // namespace platform::ui::walkie
