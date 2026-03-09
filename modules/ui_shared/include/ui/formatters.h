#pragma once

#include "lvgl.h"

void ui_format_battery(int level, bool charging, char* out, size_t out_len);
void ui_format_coords(double lat, double lon, uint8_t coord_format, char* out, size_t out_len);
