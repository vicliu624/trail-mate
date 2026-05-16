#pragma once

#include <cstdint>

namespace boards::gat562_mesh_evb_pro
{

struct Gat562BoardFacts
{
    const char* board_package = "boards/gat562_mesh_evb_pro";
    const char* board_id = "gat562";
    const char* platform_family = "nrf52";
    const char* product_name = "GAT562";

    bool display_present = true;
    int display_width = 128;
    int display_height = 64;
    const char* display_driver = "ssd1306";
    int display_i2c_sda_pin = 13;
    int display_i2c_scl_pin = 14;
    std::uint8_t display_i2c_address = 0x3C;

    bool touch_present = false;
    bool keyboard_present = false;
    bool buttons_present = true;
    bool joystick_present = true;
    int button_primary_pin = 9;
    int button_secondary_pin = 12;
    int joystick_up_pin = 28;
    int joystick_down_pin = 4;
    int joystick_left_pin = 30;
    int joystick_right_pin = 31;
    int joystick_press_pin = 26;

    bool lora_present = true;
    const char* lora_chip = "sx1262";
    int lora_sck_pin = 43;
    int lora_miso_pin = 45;
    int lora_mosi_pin = 44;
    int lora_cs_pin = 42;
    int lora_reset_pin = 38;
    int lora_busy_pin = 46;
    int lora_dio1_pin = 47;
    int lora_power_enable_pin = 37;

    bool gnss_present = true;
    int gnss_uart_rx_pin = 15;
    int gnss_uart_tx_pin = 16;
    int gnss_pps_pin = 17;
    std::uint32_t gnss_baud_rate = 9600;

    bool sd_card_present = false;
    bool internal_flash_present = true;
    int battery_adc_pin = 5;
    int peripheral_3v3_enable_pin = 34;
    std::uint32_t max_flash_size = 815104;
    std::uint32_t max_ram_size = 248832;
    std::uint32_t bootloader_settings_addr = 0xFF000;
};

inline constexpr Gat562BoardFacts kBoardFacts{};

} // namespace boards::gat562_mesh_evb_pro
