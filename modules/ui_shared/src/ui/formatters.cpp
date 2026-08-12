#include "ui/formatters.h"

#include <cmath>
#include <cstdio>

namespace
{
constexpr double kPi = 3.14159265358979323846;

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

void ui_format_battery(int level, bool charging, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (level < 0)
    {
        snprintf(out, out_len, "%s", charging ? "USB" : "--");
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
