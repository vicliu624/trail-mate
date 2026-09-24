#include "gps/gpx/track_writer.h"
#include "platform/esp/arduino_common/gps/sd_gpx_output.h"
#include "platform/ui/tracker_runtime.h"

#include "esp_timer.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/ui/gps_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

namespace platform::ui::tracker
{
namespace
{
constexpr const char* kTrackDir = "/trackers";
constexpr uint32_t kDefaultIntervalSeconds = 5;
constexpr double kDistanceOnlyThresholdMeters = 5.0;
constexpr uint32_t kMinValidEpoch = 1577836800U;

struct TrackerRuntimeState
{
    bool recording = false;
    bool auto_recording = false;
    bool distance_only = false;
    Format format = Format::GPX;
    uint32_t interval_seconds = kDefaultIntervalSeconds;
    uint64_t last_sample_ms = 0;
    bool has_last_point = false;
    double last_lat = 0.0;
    double last_lng = 0.0;
    ::platform::esp::arduino_common::storage::SdRuntimeFile file;
    std::string current_rel_path;
};

TrackerRuntimeState& state()
{
    static TrackerRuntimeState runtime;
    return runtime;
}

bool is_regular_file(const std::string& path)
{
    return ::platform::esp::arduino_common::storage::sd_exists(path.c_str()) &&
           !::platform::esp::arduino_common::storage::sd_is_directory(path.c_str());
}

std::string full_path_for(const std::string& relative)
{
    if (relative.empty())
    {
        return "/";
    }
    std::string path = relative;
    if (path.size() >= 2 && (path[0] == 'A' || path[0] == 'a') && path[1] == ':')
    {
        path.erase(0, 2);
    }
    if (path.empty())
    {
        return "/";
    }
    if (path.front() != '/')
    {
        path.insert(path.begin(), '/');
    }
    return path;
}

bool ensure_track_dir()
{
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
    {
        return false;
    }
    const std::string base = full_path_for(kTrackDir);
    return ::platform::esp::arduino_common::storage::sd_mkdir(base.c_str()) ||
           ::platform::esp::arduino_common::storage::sd_is_directory(base.c_str());
}

uint64_t now_ms()
{
    return static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
}

uint32_t now_seconds()
{
    const std::time_t epoch = std::time(nullptr);
    if (epoch >= static_cast<std::time_t>(kMinValidEpoch))
    {
        return static_cast<uint32_t>(epoch);
    }
    return static_cast<uint32_t>(now_ms() / 1000ULL);
}

std::string format_timestamp_for_name()
{
    const std::time_t epoch = std::time(nullptr);
    char buffer[32] = {};
    if (epoch >= static_cast<std::time_t>(kMinValidEpoch))
    {
        std::tm tm{};
        if (gmtime_r(&epoch, &tm) != nullptr &&
            std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &tm) > 0)
        {
            return buffer;
        }
    }
    std::snprintf(buffer,
                  sizeof(buffer),
                  "uptime_%llu",
                  static_cast<unsigned long long>(now_ms() / 1000ULL));
    return buffer;
}

const char* extension_for(Format format)
{
    switch (format)
    {
    case Format::CSV:
        return ".csv";
    case Format::Binary:
        return ".bin";
    case Format::GPX:
    default:
        return ".gpx";
    }
}

std::string make_track_path(Format format)
{
    return std::string(kTrackDir) + "/track_" + format_timestamp_for_name() +
           extension_for(format);
}

double deg_to_rad(double deg)
{
    return deg * 0.017453292519943295;
}

double distance_meters(double lat_a, double lng_a, double lat_b, double lng_b)
{
    constexpr double kEarthRadiusMeters = 6371000.0;
    const double dlat = deg_to_rad(lat_b - lat_a);
    const double dlng = deg_to_rad(lng_b - lng_a);
    const double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
                     std::cos(deg_to_rad(lat_a)) * std::cos(deg_to_rad(lat_b)) *
                         std::sin(dlng / 2.0) * std::sin(dlng / 2.0);
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return kEarthRadiusMeters * c;
}

void format_iso_time(uint32_t seconds, char* out, size_t out_len)
{
    if (out == nullptr || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (seconds < kMinValidEpoch)
    {
        return;
    }
    const std::time_t epoch = static_cast<std::time_t>(seconds);
    std::tm tm{};
    if (gmtime_r(&epoch, &tm) == nullptr)
    {
        return;
    }
    (void)std::strftime(out, out_len, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

void write_header(TrackerRuntimeState& runtime)
{
    if (!runtime.file.is_open())
    {
        return;
    }
    switch (runtime.format)
    {
    case Format::CSV:
        runtime.file.print("time,lat,lon,alt_m,speed_mps,satellites\n");
        break;
    case Format::Binary:
    {
        static constexpr char kHeader[] = "TMTRK1";
        (void)runtime.file.write(kHeader, sizeof(kHeader) - 1);
        break;
    }
    case Format::GPX:
    default:
        runtime.file.print("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                           "<gpx version=\"1.1\" creator=\"Trail Mate\">\n"
                           "  <trk><name>Trail Mate Track</name><trkseg>\n");
        break;
    }
    runtime.file.flush();
}

void write_footer(TrackerRuntimeState& runtime)
{
    if (!runtime.file.is_open())
    {
        return;
    }
    if (runtime.format == Format::GPX)
    {
        runtime.file.print("  </trkseg></trk>\n</gpx>\n");
    }
    runtime.file.flush();
}

void write_point(TrackerRuntimeState& runtime,
                 const platform::ui::gps::GpsState& gps,
                 uint32_t seconds)
{
    if (!runtime.file.is_open())
    {
        return;
    }

    switch (runtime.format)
    {
    case Format::CSV:
    {
        char iso[24] = {};
        format_iso_time(seconds, iso, sizeof(iso));
        runtime.file.printf("%s,%.7f,%.7f,%.2f,%.2f,%u\n",
                            iso[0] != '\0' ? iso : "",
                            gps.lat,
                            gps.lng,
                            gps.has_alt ? gps.alt_m : 0.0,
                            gps.has_speed ? gps.speed_mps : 0.0,
                            static_cast<unsigned>(gps.satellites));
        break;
    }
    case Format::Binary:
    {
        struct BinaryPoint
        {
            uint32_t timestamp_s;
            int32_t lat_e7;
            int32_t lon_e7;
            int16_t alt_m;
            uint16_t speed_cmps;
            uint8_t satellites;
            uint8_t flags;
        } point{};
        point.timestamp_s = seconds;
        point.lat_e7 = static_cast<int32_t>(std::lround(gps.lat * 10000000.0));
        point.lon_e7 = static_cast<int32_t>(std::lround(gps.lng * 10000000.0));
        point.alt_m = gps.has_alt ? static_cast<int16_t>(std::lround(gps.alt_m)) : 0;
        point.speed_cmps =
            gps.has_speed ? static_cast<uint16_t>(std::lround(std::max(0.0, gps.speed_mps) * 100.0)) : 0;
        point.satellites = gps.satellites;
        point.flags = static_cast<uint8_t>((gps.has_alt ? 0x01 : 0x00) |
                                           (gps.has_speed ? 0x02 : 0x00));
        (void)runtime.file.write(&point, sizeof(point));
        break;
    }
    case Format::GPX:
    default:
    {
        char iso[24] = {};
        format_iso_time(seconds, iso, sizeof(iso));
        ::gps::gpx::TrackPointOptions options;
        options.has_elevation = gps.has_alt;
        options.elevation = gps.alt_m;
        options.elevation_precision = 2;
        options.legacy_extensions = false;
        ::platform::esp::arduino_common::gps::SdGpxOutput output(runtime.file);
        (void)::gps::gpx::writeTrackPoint(output, gps.lat, gps.lng, iso, gps.satellites, 7, options);
        break;
    }
    }
    runtime.file.flush();
}

} // namespace

bool is_supported()
{
    return platform::esp::idf_common::bsp_runtime::sdcard_capable();
}

bool is_recording()
{
    return state().recording;
}

bool start_recording()
{
    TrackerRuntimeState& runtime = state();
    if (runtime.recording)
    {
        return true;
    }
    if (!ensure_track_dir())
    {
        return false;
    }

    runtime.current_rel_path = make_track_path(runtime.format);
    const std::string full_path = full_path_for(runtime.current_rel_path);
    if (!runtime.file.open(full_path.c_str(), "wb"))
    {
        runtime.current_rel_path.clear();
        return false;
    }

    runtime.recording = true;
    runtime.last_sample_ms = 0;
    runtime.has_last_point = false;
    write_header(runtime);
    poll();
    return true;
}

void stop_recording()
{
    TrackerRuntimeState& runtime = state();
    if (!runtime.recording)
    {
        return;
    }
    write_footer(runtime);
    runtime.file.close();
    runtime.recording = false;
    runtime.last_sample_ms = 0;
    runtime.has_last_point = false;
}

void poll()
{
    TrackerRuntimeState& runtime = state();
    if (!runtime.recording)
    {
        if (runtime.auto_recording)
        {
            (void)start_recording();
        }
        return;
    }
    if (!runtime.file.is_open())
    {
        stop_recording();
        return;
    }

    const uint64_t now = now_ms();
    const uint64_t interval_ms =
        static_cast<uint64_t>(std::max<uint32_t>(1, runtime.interval_seconds)) * 1000ULL;
    if (runtime.last_sample_ms != 0 && now - runtime.last_sample_ms < interval_ms)
    {
        return;
    }

    const platform::ui::gps::GpsState gps = platform::ui::gps::get_data();
    runtime.last_sample_ms = now;
    if (!gps.valid)
    {
        return;
    }
    if (runtime.distance_only && runtime.has_last_point &&
        distance_meters(runtime.last_lat, runtime.last_lng, gps.lat, gps.lng) <
            kDistanceOnlyThresholdMeters)
    {
        return;
    }

    write_point(runtime, gps, now_seconds());
    runtime.last_lat = gps.lat;
    runtime.last_lng = gps.lng;
    runtime.has_last_point = true;
}

bool current_path(std::string& out_path)
{
    out_path = state().current_rel_path;
    return !out_path.empty();
}

bool list_tracks(std::vector<std::string>& out_tracks, std::size_t max_count)
{
    out_tracks.clear();
    if (max_count == 0 || !platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
    {
        return false;
    }

    const std::string base = full_path_for(track_dir());
    ::platform::esp::arduino_common::storage::SdRuntimeDir dir;
    if (!dir.open(base.c_str()))
    {
        return false;
    }

    while (out_tracks.size() < max_count)
    {
        char name_buffer[96] = {};
        bool is_dir = false;
        if (!dir.read_next(name_buffer, sizeof(name_buffer), &is_dir))
        {
            break;
        }
        if (is_dir)
        {
            continue;
        }
        std::string name = name_buffer;
        if (name == "active.bin")
        {
            continue;
        }
        const std::string full_path = base + "/" + name;
        if (is_regular_file(full_path))
        {
            out_tracks.push_back(name);
        }
    }
    dir.close();
    std::sort(out_tracks.begin(), out_tracks.end());
    return !out_tracks.empty();
}

bool remove_track(const std::string& path)
{
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready() || path.empty())
    {
        return false;
    }
    const std::string logical_path = full_path_for(path);
    return ::platform::esp::arduino_common::storage::sd_remove(logical_path.c_str());
}

const char* track_dir()
{
    return kTrackDir;
}

void set_auto_recording(bool enabled)
{
    state().auto_recording = enabled;
}

void set_interval_seconds(uint32_t seconds)
{
    state().interval_seconds = std::max<uint32_t>(1, seconds);
}

void set_distance_only(bool enabled)
{
    state().distance_only = enabled;
}

void set_format(Format format)
{
    if (!state().recording)
    {
        state().format = format;
    }
}

} // namespace platform::ui::tracker
