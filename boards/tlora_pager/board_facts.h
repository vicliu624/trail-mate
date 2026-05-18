#pragma once

namespace boards::tlora_pager
{

struct TLoRaPagerBoardFacts
{
    const char* board_package = "boards/tlora_pager";
    const char* board_id = "tlora_pager";
    const char* documented_board_id = "t_lora_pager";
    const char* platform_family = "esp32";

    bool display_present = true;
    int display_width = 222;
    int display_height = 480;
    int logical_width = 480;
    int logical_height = 222;
    const char* display_driver = "st7796";
    bool touch_present = false;
    bool keyboard_present = true;
    bool buttons_present = true;
    bool rotary_encoder_present = true;
    bool trackball_present = false;
    bool lora_present = true;
    const char* lora_chip = "sx1262";
    bool gps_present = true;
    bool sd_card_present = true;
    bool audio_codec_present = true;
    bool motion_sensor_present = true;
    bool rtc_present = true;
    bool nfc_present = true;
};

inline constexpr TLoRaPagerBoardFacts kBoardFacts{};

} // namespace boards::tlora_pager
