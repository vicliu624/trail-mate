#pragma once

#include <cstdint>

namespace boards::tdisplayp4
{

struct TDisplayP4BoardFacts
{
    const char* board_package = "boards/t_display_p4";
    const char* board_id = "tdisplayp4";
    const char* platform_family = "esp32";
    const char* product_name = "TrailMate P4";

    bool display_present = true;
    int hi8561_width = 540;
    int hi8561_height = 1168;
    int rm69a10_width = 568;
    int rm69a10_height = 1232;
    bool touch_present = true;
    bool keyboard_present = false;
    bool trackball_present = false;
    bool audio_present = true;
    bool sd_card_present = true;
    bool gps_uart_present = true;
    bool lora_present = true;

    int sys_i2c_sda_pin = 7;
    int sys_i2c_scl_pin = 8;
    int ext_i2c_sda_pin = 20;
    int ext_i2c_scl_pin = 21;
    int gps_uart_tx_pin = 22;
    int gps_uart_rx_pin = 23;
    std::uint16_t hi8561_touch_address = 0x68;
    std::uint16_t rm69a10_touch_address = 0x5D;
};

inline constexpr TDisplayP4BoardFacts kBoardFacts{};

} // namespace boards::tdisplayp4
