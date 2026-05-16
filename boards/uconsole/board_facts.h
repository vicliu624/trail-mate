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
    const char* gps_state = "optional";
    const char* gps_device_source = "/dev/serial/by-id_or_env_override";
    bool posix_filesystem_present = true;
};

inline constexpr UConsoleBoardFacts kBoardFacts{};

} // namespace boards::uconsole
