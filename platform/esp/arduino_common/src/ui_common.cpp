/**
 * @file ui_common.cpp
 * @brief Common UI functions implementation
 */

#include "ui/ui_common.h"
#include "board/BoardBase.h"
#include "lvgl.h"
#include "platform/ui/settings_store.h"
#if LV_USE_SNAPSHOT
extern "C" lv_draw_buf_t* lv_snapshot_take(lv_obj_t* obj, lv_color_format_t cf);
extern "C" void lv_draw_buf_destroy(lv_draw_buf_t* draw_buf);
#endif
#include <SD.h>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>

extern BoardBase& board;

namespace
{
constexpr const char* kPrefsNs = "settings";
constexpr const char* kTimezoneKey = "timezone_offset";

static bool s_tz_loaded = false;
static int s_tz_offset_min = 0;
} // namespace

void ui_update_top_bar_battery(ui::widgets::TopBar& bar)
{
    char battery_buf[24] = "--";
    int battery = board.getBatteryLevel();
    bool charging = board.isCharging();
    ui_format_battery(battery, charging, battery_buf, sizeof(battery_buf));
    ui::widgets::top_bar_set_right_text_ascii(bar, battery_buf);
}

int ui_get_timezone_offset_min()
{
    if (!s_tz_loaded)
    {
        s_tz_offset_min = ::platform::ui::settings_store::get_int(kPrefsNs, kTimezoneKey, 0);
        s_tz_loaded = true;
    }
    return s_tz_offset_min;
}

void ui_set_timezone_offset_min(int offset_min)
{
    s_tz_offset_min = offset_min;
    s_tz_loaded = true;
    ::platform::ui::settings_store::put_int(kPrefsNs, kTimezoneKey, offset_min);
}

time_t ui_apply_timezone_offset(time_t utc_seconds)
{
    if (utc_seconds <= 0)
    {
        return utc_seconds;
    }
    int offset_min = ui_get_timezone_offset_min();
    return utc_seconds + static_cast<time_t>(offset_min) * 60;
}

bool ui_take_screenshot_to_sd()
{
#if LV_USE_SNAPSHOT
    if (SD.cardType() == CARD_NONE)
    {
        Serial.println("[Screenshot] SD not available");
        return false;
    }

    if (!SD.exists("/screen"))
    {
        if (!SD.mkdir("/screen"))
        {
            Serial.println("[Screenshot] mkdir /screen failed");
        }
    }

    lv_obj_t* screen = lv_screen_active();
    if (!screen)
    {
        Serial.println("[Screenshot] No active screen");
        return false;
    }

    lv_draw_buf_t* snap = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB565);
    if (!snap)
    {
        Serial.println("[Screenshot] Snapshot failed");
        return false;
    }

    uint32_t w = snap->header.w;
    uint32_t h = snap->header.h;
    uint32_t row_bytes = snap->header.stride;
    if (row_bytes == 0)
    {
        row_bytes = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565);
    }
    uint32_t row24 = (w * 3 + 3) & ~3u;
    uint32_t pixel_bytes = row24 * h;

    uint32_t file_size = 14 + 40 + pixel_bytes;
    uint32_t data_offset = 14 + 40;

    char path[64];
    time_t now = time(nullptr);
    time_t local = ui_apply_timezone_offset(now);
    struct tm* info = gmtime(&local);
    if (info)
    {
        char ts[20];
        strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", info);
        snprintf(path, sizeof(path), "/screen/screenshot_%s.bmp", ts);
    }
    else
    {
        snprintf(path, sizeof(path), "/screen/screenshot_%lu.bmp",
                 static_cast<unsigned long>(millis()));
    }

    File f = SD.open(path, FILE_WRITE);
    if (!f)
    {
        Serial.printf("[Screenshot] Open failed: %s\n", path);
        lv_draw_buf_destroy(snap);
        return false;
    }

    // BITMAPFILEHEADER
    uint8_t file_hdr[14] = {
        'B', 'M',
        static_cast<uint8_t>(file_size & 0xFF),
        static_cast<uint8_t>((file_size >> 8) & 0xFF),
        static_cast<uint8_t>((file_size >> 16) & 0xFF),
        static_cast<uint8_t>((file_size >> 24) & 0xFF),
        0, 0, 0, 0,
        static_cast<uint8_t>(data_offset & 0xFF),
        static_cast<uint8_t>((data_offset >> 8) & 0xFF),
        static_cast<uint8_t>((data_offset >> 16) & 0xFF),
        static_cast<uint8_t>((data_offset >> 24) & 0xFF)};
    f.write(file_hdr, sizeof(file_hdr));

    // BITMAPINFOHEADER (24-bit, no compression)
    uint8_t info_hdr[40] = {0};
    info_hdr[0] = 40; // header size
    info_hdr[4] = static_cast<uint8_t>(w & 0xFF);
    info_hdr[5] = static_cast<uint8_t>((w >> 8) & 0xFF);
    info_hdr[6] = static_cast<uint8_t>((w >> 16) & 0xFF);
    info_hdr[7] = static_cast<uint8_t>((w >> 24) & 0xFF);
    info_hdr[8] = static_cast<uint8_t>(h & 0xFF);
    info_hdr[9] = static_cast<uint8_t>((h >> 8) & 0xFF);
    info_hdr[10] = static_cast<uint8_t>((h >> 16) & 0xFF);
    info_hdr[11] = static_cast<uint8_t>((h >> 24) & 0xFF);
    info_hdr[12] = 1;  // planes
    info_hdr[14] = 24; // bpp
    info_hdr[16] = 0;  // BI_RGB
    info_hdr[20] = static_cast<uint8_t>(pixel_bytes & 0xFF);
    info_hdr[21] = static_cast<uint8_t>((pixel_bytes >> 8) & 0xFF);
    info_hdr[22] = static_cast<uint8_t>((pixel_bytes >> 16) & 0xFF);
    info_hdr[23] = static_cast<uint8_t>((pixel_bytes >> 24) & 0xFF);
    f.write(info_hdr, sizeof(info_hdr));

    const uint8_t* pixels = snap->data;
    std::vector<uint8_t> rowbuf(row24, 0);
    for (uint32_t y = 0; y < h; ++y)
    {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(
            pixels + (h - 1 - y) * row_bytes);
        size_t idx = 0;
        for (uint32_t x = 0; x < w; ++x)
        {
            uint16_t px = row[x];
            uint8_t r5 = (px >> 11) & 0x1F;
            uint8_t g6 = (px >> 5) & 0x3F;
            uint8_t b5 = px & 0x1F;
            uint8_t r = (r5 << 3) | (r5 >> 2);
            uint8_t g = (g6 << 2) | (g6 >> 4);
            uint8_t b = (b5 << 3) | (b5 >> 2);
            rowbuf[idx++] = b;
            rowbuf[idx++] = g;
            rowbuf[idx++] = r;
        }
        f.write(rowbuf.data(), rowbuf.size());
    }

    f.flush();
    f.close();
    lv_draw_buf_destroy(snap);
    Serial.printf("[Screenshot] Saved: %s\n", path);
    return true;
#else
    Serial.println("[Screenshot] LV_USE_SNAPSHOT disabled");
    return false;
#endif
}
