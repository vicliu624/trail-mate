#pragma once

namespace boards::twatch
{

struct TWatchBoardFacts
{
    const char* board_package = "boards/twatchs3";
    const char* board_id = "twatch";
    const char* platform_family = "esp32";
    const char* product_name = "LilyGo T-Watch-S3";

    bool display_present = true;
    int display_width = 240;
    int display_height = 240;
    const char* display_driver_define = "DISPLAY_DRIVER_ST7789V2";
    bool touch_present = true;
    bool keyboard_present = false;
    bool trackball_present = false;
    bool sd_card_present = false;
    bool gps_present = false;
    bool lora_present = true;
    bool psram_present = true;
};

inline constexpr TWatchBoardFacts kBoardFacts{};

} // namespace boards::twatch
