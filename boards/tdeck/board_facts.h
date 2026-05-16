#pragma once

namespace boards::tdeck
{

struct TDeckBoardFacts
{
    const char* board_package = "boards/tdeck";
    const char* board_id = "tdeck";
    const char* documented_board_id = "t_deck";
    const char* platform_family = "esp32";

    bool display_present = true;
    int display_width = 320;
    int display_height = 240;
    const char* display_driver = "st7789";
    bool touch_present = true;
    bool keyboard_present = true;
    bool buttons_present = true;
    bool rotary_encoder_present = false;
    bool trackball_present = true;
    bool lora_present = true;
    const char* lora_chip = "sx1262";
    bool gps_present = true;
    bool sd_card_present = true;
    bool audio_codec_present = true;
    const char* rtc_state = "unknown";
    const char* motion_sensor_state = "unknown";
};

inline constexpr TDeckBoardFacts kBoardFacts{};

} // namespace boards::tdeck
