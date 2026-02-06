#pragma once

#include <cstdint>

namespace walkie
{
struct Status
{
    bool active;
    bool tx;
    uint8_t tx_level;
    uint8_t rx_level;
    float freq_mhz;
};

bool start();
void stop();
bool is_active();
void set_ptt(bool pressed);
void adjust_volume(int delta);
int get_volume();
void on_key_event(char key, int state);
Status get_status();
const char* get_last_error();
} // namespace walkie
