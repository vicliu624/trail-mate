#pragma once

namespace boards::uconsole
{

struct UConsoleBoardFacts
{
    const char* board_package = "platform/linux/uconsole";
    const char* board_id = "uconsole";
    const char* documented_board_id = "uconsole_aio2";
    const char* platform_family = "linux";

    bool display_present = true;
    int display_width = 0;
    int display_height = 0;
    const char* display_geometry_source = "host_window_or_framebuffer";
    bool keyboard_present = true;
    bool pointer_present = true;
    const char* touch_state = "host_dependent";
    bool trackball_present = false;
    const char* lora_state = "optional";
    const char* lora_default_spi = "/dev/spidev1.0";
    const char* lora_power_gpio = "GPIO16";
    const char* lora_reset_gpio = "GPIO25";
    const char* lora_busy_gpio = "GPIO24";
    const char* lora_irq_gpio = "GPIO26";
    const char* gps_state = "optional";
    const char* gps_device_source =
        "AIO2 CM4 default /dev/ttyS0 @ 9600; TRAIL_MATE_GPS_DEVICE overrides";
    const char* gps_power_gpio = "GPIO27";
    bool gps_usb_control_serial_is_receiver = false;
    bool posix_filesystem_present = true;
};

inline constexpr UConsoleBoardFacts kBoardFacts{};

} // namespace boards::uconsole
