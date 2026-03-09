#pragma once

namespace platform::esp::idf_common::bsp_runtime
{

bool ensure_nvs_ready();
bool ensure_sdcard_ready();
bool sdcard_ready();
const char* sdcard_mount_point();
bool display_ready();
bool set_display_brightness(int brightness_percent);
bool wake_display();
bool sleep_display();
int default_awake_brightness_percent();
bool gps_capable();
bool sdcard_capable();

} // namespace platform::esp::idf_common::bsp_runtime
