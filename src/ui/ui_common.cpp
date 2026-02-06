/**
 * @file ui_common.cpp
 * @brief Common UI functions implementation
 */

#include "ui_common.h"
#include "../board/BoardBase.h"
#include "lvgl.h"
#if LV_USE_SNAPSHOT
extern "C" lv_draw_buf_t* lv_snapshot_take(lv_obj_t* obj, lv_color_format_t cf);
extern "C" void lv_draw_buf_destroy(lv_draw_buf_t* draw_buf);
#endif
#include <Preferences.h>
#include <SD.h>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>

extern BoardBase& board;

namespace
{
constexpr const char* kPrefsNs = "settings_v2";
constexpr const char* kTimezoneKey = "timezone_offset";

static bool s_tz_loaded = false;
static int s_tz_offset_min = 0;
AppScreen* s_active_app = nullptr;
AppScreen* s_pending_exit = nullptr;
lv_timer_t* s_exit_timer = nullptr;
constexpr uint32_t kExitDelayMs = 120;
constexpr double kPi = 3.14159265358979323846;
} // namespace

void set_default_group(lv_group_t* group)
{
    lv_indev_t* cur_drv = NULL;
    for (;;)
    {
        cur_drv = lv_indev_get_next(cur_drv);
        if (!cur_drv)
        {
            break;
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_KEYPAD)
        {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_ENCODER)
        {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_POINTER)
        {
            lv_indev_set_group(cur_drv, group);
        }
    }
    lv_group_set_default(group);
}

void menu_show()
{
    ui_clear_active_app();
    set_default_group(menu_g);
    lv_tileview_set_tile_by_index(main_screen, 0, 0, LV_ANIM_ON);
}

AppScreen* ui_get_active_app()
{
    return s_active_app;
}

void ui_clear_active_app()
{
    s_active_app = nullptr;
}

void ui_switch_to_app(AppScreen* app, lv_obj_t* parent)
{
    if (s_pending_exit == app)
    {
        s_pending_exit = nullptr;
    }
    if (s_active_app && s_active_app != app)
    {
        s_active_app->exit(parent);
    }
    if (app)
    {
        app->enter(parent);
    }
    s_active_app = app;
}

void ui_exit_active_app(lv_obj_t* parent)
{
    if (s_active_app)
    {
        s_active_app->exit(parent);
    }
    s_active_app = nullptr;
}

namespace
{
void exit_to_menu_timer_cb(lv_timer_t* timer)
{
    auto* app = static_cast<AppScreen*>(timer ? timer->user_data : nullptr);
    if (timer)
    {
        lv_timer_del(timer);
    }
    s_exit_timer = nullptr;
    if (!app || s_pending_exit != app)
    {
        return;
    }
    s_pending_exit = nullptr;
    if (main_screen == nullptr)
    {
        app->exit(nullptr);
        return;
    }
    lv_obj_t* parent = lv_obj_get_child(main_screen, 1);
    app->exit(parent);
    if (menu_g)
    {
        set_default_group(menu_g);
        lv_group_set_editing(menu_g, false);
    }
}
} // namespace

void ui_request_exit_to_menu()
{
    AppScreen* app = s_active_app;
    menu_show();
    if (app == nullptr)
    {
        return;
    }
    if (s_pending_exit == app)
    {
        return;
    }
    s_pending_exit = app;
    if (s_exit_timer)
    {
        lv_timer_del(s_exit_timer);
        s_exit_timer = nullptr;
    }
    s_exit_timer = lv_timer_create(exit_to_menu_timer_cb, kExitDelayMs, app);
    if (s_exit_timer)
    {
        lv_timer_set_repeat_count(s_exit_timer, 1);
    }
}

lv_obj_t* create_menu(lv_obj_t* parent, lv_event_cb_t event_cb)
{
    lv_obj_t* menu = lv_menu_create(parent);
    lv_menu_set_mode_root_back_button(menu, LV_MENU_ROOT_BACK_BUTTON_ENABLED);
    lv_obj_add_event_cb(menu, event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_size(menu, LV_PCT(100), LV_PCT(100));
    lv_obj_center(menu);
    return menu;
}

void ui_format_battery(int level, bool charging, char* out, size_t out_len)
{
    if (!out || out_len == 0) return;
    if (level < 0)
    {
        snprintf(out, out_len, "?%%");
        return;
    }

    const char* symbol;
    if (charging)
    {
        symbol = LV_SYMBOL_CHARGE;
    }
    else if (level >= 90)
    {
        symbol = LV_SYMBOL_BATTERY_FULL;
    }
    else if (level >= 60)
    {
        symbol = LV_SYMBOL_BATTERY_3;
    }
    else if (level >= 30)
    {
        symbol = LV_SYMBOL_BATTERY_2;
    }
    else if (level >= 10)
    {
        symbol = LV_SYMBOL_BATTERY_1;
    }
    else
    {
        symbol = LV_SYMBOL_BATTERY_EMPTY;
    }
    snprintf(out, out_len, "%s %d%%", symbol, level);
}

void ui_update_top_bar_battery(ui::widgets::TopBar& bar)
{
    char battery_buf[24] = "?%";
    int battery = board.getBatteryLevel();
    bool charging = board.isCharging();
    ui_format_battery(battery, charging, battery_buf, sizeof(battery_buf));
    ui::widgets::top_bar_set_right_text(bar, battery_buf);
}

int ui_get_timezone_offset_min()
{
    if (!s_tz_loaded)
    {
        Preferences prefs;
        prefs.begin(kPrefsNs, true);
        s_tz_offset_min = prefs.getInt(kTimezoneKey, 0);
        prefs.end();
        s_tz_loaded = true;
    }
    return s_tz_offset_min;
}

void ui_set_timezone_offset_min(int offset_min)
{
    s_tz_offset_min = offset_min;
    s_tz_loaded = true;
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

namespace
{
void format_dms(double value, bool is_lat, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    char hemi = is_lat ? (value >= 0.0 ? 'N' : 'S') : (value >= 0.0 ? 'E' : 'W');
    double abs_val = std::fabs(value);
    int deg = static_cast<int>(abs_val);
    double minutes_f = (abs_val - deg) * 60.0;
    int minutes = static_cast<int>(minutes_f);
    double seconds = (minutes_f - minutes) * 60.0;
    snprintf(out, out_len, "%c %d%c%02d'%05.2f\"",
             hemi, deg, 'd', minutes, seconds);
}

bool latlon_to_utm(double lat, double lon, int& zone, char& hemi, double& easting, double& northing)
{
    if (lat < -80.0 || lat > 84.0)
    {
        return false;
    }

    zone = static_cast<int>((lon + 180.0) / 6.0) + 1;
    double lon_origin = (zone - 1) * 6.0 - 180.0 + 3.0;

    constexpr double kA = 6378137.0;
    constexpr double kF = 1.0 / 298.257223563;
    constexpr double kE2 = kF * (2.0 - kF);
    constexpr double kEPrime2 = kE2 / (1.0 - kE2);
    constexpr double kK0 = 0.9996;

    double lat_rad = lat * kPi / 180.0;
    double lon_rad = lon * kPi / 180.0;
    double lon_origin_rad = lon_origin * kPi / 180.0;

    double sin_lat = std::sin(lat_rad);
    double cos_lat = std::cos(lat_rad);
    double tan_lat = std::tan(lat_rad);

    double N = kA / std::sqrt(1.0 - kE2 * sin_lat * sin_lat);
    double T = tan_lat * tan_lat;
    double C = kEPrime2 * cos_lat * cos_lat;
    double A = cos_lat * (lon_rad - lon_origin_rad);

    double M = kA * ((1.0 - kE2 / 4.0 - 3.0 * kE2 * kE2 / 64.0 - 5.0 * kE2 * kE2 * kE2 / 256.0) * lat_rad - (3.0 * kE2 / 8.0 + 3.0 * kE2 * kE2 / 32.0 + 45.0 * kE2 * kE2 * kE2 / 1024.0) * std::sin(2.0 * lat_rad) + (15.0 * kE2 * kE2 / 256.0 + 45.0 * kE2 * kE2 * kE2 / 1024.0) * std::sin(4.0 * lat_rad) - (35.0 * kE2 * kE2 * kE2 / 3072.0) * std::sin(6.0 * lat_rad));

    easting = kK0 * N * (A + (1.0 - T + C) * A * A * A / 6.0 + (5.0 - 18.0 * T + T * T + 72.0 * C - 58.0 * kEPrime2) * A * A * A * A * A / 120.0) +
              500000.0;

    northing = kK0 * (M + N * tan_lat * (A * A / 2.0 + (5.0 - T + 9.0 * C + 4.0 * C * C) * A * A * A * A / 24.0 + (61.0 - 58.0 * T + T * T + 600.0 * C - 330.0 * kEPrime2) * A * A * A * A * A * A / 720.0));

    hemi = (lat >= 0.0) ? 'N' : 'S';
    if (lat < 0.0)
    {
        northing += 10000000.0;
    }
    return true;
}
} // namespace

void ui_format_coords(double lat, double lon, uint8_t coord_format, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';

    if (coord_format == 1)
    {
        char lat_buf[32];
        char lon_buf[32];
        format_dms(lat, true, lat_buf, sizeof(lat_buf));
        format_dms(lon, false, lon_buf, sizeof(lon_buf));
        snprintf(out, out_len, "%s, %s", lat_buf, lon_buf);
        return;
    }

    if (coord_format == 2)
    {
        int zone = 0;
        char hemi = 'N';
        double easting = 0.0;
        double northing = 0.0;
        if (latlon_to_utm(lat, lon, zone, hemi, easting, northing))
        {
            snprintf(out, out_len, "UTM %02d%c %.0f %.0f",
                     zone, hemi, easting, northing);
            return;
        }
    }

    snprintf(out, out_len, "%.5f, %.5f", lat, lon);
}
