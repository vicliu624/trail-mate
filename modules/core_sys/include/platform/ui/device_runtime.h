#pragma once

#include <cstdint>

namespace platform::ui::device
{

struct BatteryInfo
{
    bool available = false;
    bool charging = false;
    int level = -1;
};

void delay_ms(uint32_t ms);
void restart();
bool rtc_ready();
BatteryInfo battery_info();
void handle_low_battery(const BatteryInfo& info);
bool supports_screen_brightness();
uint8_t screen_brightness();
void set_screen_brightness(uint8_t level);
void trigger_haptic();
uint8_t default_message_tone_volume();
void set_message_tone_volume(uint8_t volume_percent);
void play_message_tone();
bool sd_ready();
bool card_ready();
bool gps_ready();
int power_tier();

} // namespace platform::ui::device
