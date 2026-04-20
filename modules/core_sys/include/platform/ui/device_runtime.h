#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::ui::device
{

struct BatteryInfo
{
    bool available = false;
    bool charging = false;
    int level = -1;
};

struct MemoryStats
{
    std::size_t ram_total_bytes = 0;
    std::size_t ram_free_bytes = 0;
    std::size_t psram_total_bytes = 0;
    std::size_t psram_free_bytes = 0;
    bool psram_available = false;
};

void delay_ms(uint32_t ms);
void restart();
bool rtc_ready();
BatteryInfo battery_info();
MemoryStats memory_stats();
const char* firmware_version();
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
// Runtime state: GPS is currently online/powered/producing service.
bool gps_ready();
// Stable capability: this target has GPS hardware support and may expose GPS UI.
bool gps_supported();
int power_tier();

} // namespace platform::ui::device
