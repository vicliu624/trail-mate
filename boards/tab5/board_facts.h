#pragma once

namespace boards::tab5
{

struct Tab5BoardFacts
{
    const char* board_package = "boards/tab5";
    const char* board_id = "tab5";
    const char* platform_family = "esp32";
    const char* product_name = "Trail Mate Tab5";

    bool display_present = true;
    int display_width = 0;
    int display_height = 0;
    const char* display_size_source = "repo_evidence_pending";
    bool touch_present = true;
    bool keyboard_present = false;
    bool trackball_present = false;
    bool audio_present = true;
    bool sd_card_present = true;
    bool gps_uart_present = true;
    bool rs485_uart_present = true;
    bool lora_present = true;
    bool m5bus_lora_module_routing_present = true;

    int sys_i2c_sda_pin = 31;
    int sys_i2c_scl_pin = 32;
    int ext_i2c_sda_pin = 53;
    int ext_i2c_scl_pin = 54;
    int gps_uart_tx_pin = 6;
    int gps_uart_rx_pin = 7;
    int lcd_backlight_pin = 22;
    int touch_interrupt_pin = 23;
};

inline constexpr Tab5BoardFacts kBoardFacts{};

} // namespace boards::tab5
