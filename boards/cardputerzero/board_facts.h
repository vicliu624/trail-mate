#pragma once

namespace boards::cardputerzero
{

struct CardputerZeroBoardFacts
{
    const char* board_id = "cardputerzero";
    const char* platform_family = "linux";
    const char* current_route_evidence = "docs/targets/linux_targets.md";

    bool display_present = true;
    int logical_display_width = 320;
    int logical_display_height = 170;
    const char* logical_display_source = "platform/linux/common/src/core/display_profile.h";
    bool keyboard_present = true;
    const char* keyboard_mapping_state = "needs_real_device_sampling";
    bool pointer_present = false;
    bool touch_present = false;
    bool trackball_present = false;
    const char* lora_state = "not_established_in_final_owner";
    const char* gps_state = "not_established_in_final_owner";
    bool posix_filesystem_present = true;
};

inline constexpr CardputerZeroBoardFacts kBoardFacts{};

} // namespace boards::cardputerzero
