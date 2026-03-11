#include "ui/ui_common.h"

#include <cstdio>
#include <ctime>
#include <vector>

#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/settings_store.h"

extern "C" lv_draw_buf_t* lv_snapshot_take(lv_obj_t* obj, lv_color_format_t cf);
extern "C" void lv_draw_buf_destroy(lv_draw_buf_t* draw_buf);

namespace
{

constexpr const char* kSettingsNs = "settings";
constexpr const char* kTimezoneKey = "timezone_offset";
int s_timezone_offset_min = 0;
bool s_timezone_loaded = false;

void ensure_timezone_loaded()
{
    if (s_timezone_loaded)
    {
        return;
    }
    s_timezone_offset_min = platform::ui::settings_store::get_int(kSettingsNs, kTimezoneKey, 0);
    s_timezone_loaded = true;
}

} // namespace

void ui_update_top_bar_battery(ui::widgets::TopBar& bar)
{
    const platform::ui::device::BatteryInfo info = platform::ui::device::battery_info();
    if (info.level < 0)
    {
        ui::widgets::top_bar_set_right_text(bar, info.charging ? "USB" : "--");
        return;
    }

    char battery_buf[32] = "--";
    ui_format_battery(info.level, info.charging, battery_buf, sizeof(battery_buf));
    ui::widgets::top_bar_set_right_text(bar, battery_buf);
}

int ui_get_timezone_offset_min()
{
    ensure_timezone_loaded();
    return s_timezone_offset_min;
}

void ui_set_timezone_offset_min(int offset_min)
{
    s_timezone_offset_min = offset_min;
    s_timezone_loaded = true;
    platform::ui::settings_store::put_int(kSettingsNs, kTimezoneKey, offset_min);
}

time_t ui_apply_timezone_offset(time_t utc_seconds)
{
    return utc_seconds + static_cast<time_t>(ui_get_timezone_offset_min()) * 60;
}

bool ui_take_screenshot_to_sd()
{
#if LV_USE_SNAPSHOT
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
    {
        return false;
    }

    lv_obj_t* screen = lv_screen_active();
    if (!screen)
    {
        return false;
    }

    lv_draw_buf_t* snap = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB565);
    if (!snap)
    {
        return false;
    }

    const uint32_t w = snap->header.w;
    const uint32_t h = snap->header.h;
    uint32_t row_bytes = snap->header.stride;
    if (row_bytes == 0)
    {
        row_bytes = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565);
    }
    const uint32_t row24 = (w * 3 + 3) & ~3u;
    const uint32_t pixel_bytes = row24 * h;
    const uint32_t file_size = 14 + 40 + pixel_bytes;
    const uint32_t data_offset = 14 + 40;

    char ts[32] = {0};
    char path[128] = {0};
    const time_t now = time(nullptr);
    const time_t local = ui_apply_timezone_offset(now);
    struct tm info
    {
    };
    if (gmtime_r(&local, &info) != nullptr)
    {
        strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &info);
    }
    else
    {
        snprintf(ts, sizeof(ts), "%lu", static_cast<unsigned long>(now > 0 ? now : 0));
    }
    snprintf(path,
             sizeof(path),
             "%s/screenshot_%s.bmp",
             platform::esp::idf_common::bsp_runtime::sdcard_mount_point(),
             ts);

    FILE* file = fopen(path, "wb");
    if (!file)
    {
        lv_draw_buf_destroy(snap);
        return false;
    }

    const uint8_t file_hdr[14] = {
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
    fwrite(file_hdr, 1, sizeof(file_hdr), file);

    uint8_t info_hdr[40] = {0};
    info_hdr[0] = 40;
    info_hdr[4] = static_cast<uint8_t>(w & 0xFF);
    info_hdr[5] = static_cast<uint8_t>((w >> 8) & 0xFF);
    info_hdr[6] = static_cast<uint8_t>((w >> 16) & 0xFF);
    info_hdr[7] = static_cast<uint8_t>((w >> 24) & 0xFF);
    info_hdr[8] = static_cast<uint8_t>(h & 0xFF);
    info_hdr[9] = static_cast<uint8_t>((h >> 8) & 0xFF);
    info_hdr[10] = static_cast<uint8_t>((h >> 16) & 0xFF);
    info_hdr[11] = static_cast<uint8_t>((h >> 24) & 0xFF);
    info_hdr[12] = 1;
    info_hdr[14] = 24;
    info_hdr[20] = static_cast<uint8_t>(pixel_bytes & 0xFF);
    info_hdr[21] = static_cast<uint8_t>((pixel_bytes >> 8) & 0xFF);
    info_hdr[22] = static_cast<uint8_t>((pixel_bytes >> 16) & 0xFF);
    info_hdr[23] = static_cast<uint8_t>((pixel_bytes >> 24) & 0xFF);
    fwrite(info_hdr, 1, sizeof(info_hdr), file);

    const uint8_t* pixels = static_cast<const uint8_t*>(snap->data);
    std::vector<uint8_t> rowbuf(row24, 0);
    for (uint32_t y = 0; y < h; ++y)
    {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(pixels + (h - 1 - y) * row_bytes);
        size_t idx = 0;
        for (uint32_t x = 0; x < w; ++x)
        {
            const uint16_t px = row[x];
            const uint8_t r5 = (px >> 11) & 0x1F;
            const uint8_t g6 = (px >> 5) & 0x3F;
            const uint8_t b5 = px & 0x1F;
            const uint8_t r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
            const uint8_t g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
            const uint8_t b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
            rowbuf[idx++] = b;
            rowbuf[idx++] = g;
            rowbuf[idx++] = r;
        }
        fwrite(rowbuf.data(), 1, rowbuf.size(), file);
    }

    fflush(file);
    fclose(file);
    lv_draw_buf_destroy(snap);
    return true;
#else
    return false;
#endif
}
