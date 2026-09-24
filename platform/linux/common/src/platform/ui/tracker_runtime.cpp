#include "platform/ui/tracker_runtime.h"
#include "gps/gpx/ostream_sink.h"
#include "gps/gpx/track_writer.h"
#include <ctime>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "platform/linux/runtime_paths.h"
#include "platform/ui/gps_runtime.h"
#include "sys/clock.h"

namespace platform::ui::tracker
{
namespace
{

bool s_recording = false;
std::string s_current_path{};
bool s_auto_recording = false;
bool s_started_automatically = false;
uint32_t s_interval_seconds = 60;
bool s_distance_only = false;
Format s_format = Format::GPX;
std::string s_track_dir_cache{};
uint32_t s_last_sample_ms = 0;
bool s_has_last_point = false;
double s_last_lat = 0.0;
double s_last_lng = 0.0;

std::filesystem::path track_dir_path()
{
    return ::platform::linux_runtime::resolve_paths().sd_root / "tracks";
}

bool ensure_track_dir()
{
    std::error_code ec;
    const std::filesystem::path dir = track_dir_path();
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        return false;
    }
    s_track_dir_cache = dir.string();
    return true;
}

std::string extension_for_format(Format format)
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

std::string make_track_file_name()
{
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    return "track-" + std::to_string(seconds) + extension_for_format(s_format);
}

bool should_sample_now(uint32_t now_ms)
{
    if (!s_recording || s_current_path.empty())
    {
        return false;
    }
    if (s_last_sample_ms == 0)
    {
        return true;
    }
    const uint32_t interval_ms = std::max<uint32_t>(1U, s_interval_seconds) * 1000U;
    return static_cast<int32_t>(now_ms - s_last_sample_ms) >= static_cast<int32_t>(interval_ms);
}

bool distance_gate_allows(const platform::ui::gps::GpsState& state)
{
    if (!s_distance_only || !s_has_last_point)
    {
        return true;
    }
    constexpr double kMinDeltaDeg = 0.00001;
    return std::abs(state.lat - s_last_lat) >= kMinDeltaDeg ||
           std::abs(state.lng - s_last_lng) >= kMinDeltaDeg;
}

void append_csv_point(std::ofstream& stream,
                      const platform::ui::gps::GpsState& state,
                      uint32_t epoch_s)
{
    stream << epoch_s << ','
           << std::fixed << std::setprecision(7) << state.lat << ','
           << std::fixed << std::setprecision(7) << state.lng << ','
           << std::fixed << std::setprecision(1) << state.alt_m << ','
           << std::fixed << std::setprecision(2) << state.speed_mps << ','
           << std::fixed << std::setprecision(1) << state.course_deg << '\n';
}

void append_gpx_point(std::ofstream& stream,
                      const platform::ui::gps::GpsState& state,
                      uint32_t epoch_s)
{
    char iso[32]{};
    const std::time_t seconds = static_cast<std::time_t>(epoch_s);
    if (epoch_s != 0)
    {
        const std::tm* utc = std::gmtime(&seconds);
        if (utc) std::strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", utc);
    }
    ::gps::gpx::TrackPointOptions options;
    options.has_elevation = state.has_alt;
    options.elevation = state.alt_m;
    options.legacy_extensions = false;
    options.has_speed = state.has_speed;
    options.speed_mps = state.speed_mps;
    options.has_course = state.has_course;
    options.course_deg = state.course_deg;
    ::gps::gpx::OstreamSink output(stream);
    if (!::gps::gpx::writeTrackPoint(output, state.lat, state.lng, iso, state.satellites, 7, options))
        stream.setstate(std::ios::badbit);
}

void append_binary_point(std::ofstream& stream,
                         const platform::ui::gps::GpsState& state,
                         uint32_t epoch_s)
{
    struct BinaryPoint
    {
        uint32_t epoch_s = 0;
        double lat = 0.0;
        double lng = 0.0;
        float alt_m = 0.0f;
        float speed_mps = 0.0f;
        float course_deg = 0.0f;
    };

    const BinaryPoint point{
        epoch_s,
        state.lat,
        state.lng,
        static_cast<float>(state.alt_m),
        static_cast<float>(state.speed_mps),
        static_cast<float>(state.course_deg),
    };
    stream.write(reinterpret_cast<const char*>(&point), sizeof(point));
}

bool append_track_point(const platform::ui::gps::GpsState& state)
{
    if (!state.valid || s_current_path.empty())
    {
        return false;
    }
    if (!distance_gate_allows(state))
    {
        return false;
    }

    std::ofstream stream(s_current_path, std::ios::binary | std::ios::app);
    if (!stream.is_open())
    {
        return false;
    }

    const uint32_t epoch_s = sys::epoch_seconds_now();
    switch (s_format)
    {
    case Format::CSV:
        append_csv_point(stream, state, epoch_s);
        break;
    case Format::Binary:
        append_binary_point(stream, state, epoch_s);
        break;
    case Format::GPX:
    default:
        append_gpx_point(stream, state, epoch_s);
        break;
    }

    s_has_last_point = true;
    s_last_lat = state.lat;
    s_last_lng = state.lng;
    return true;
}

} // namespace

bool is_supported()
{
    return ensure_track_dir();
}

bool is_recording()
{
    return s_recording;
}

bool start_recording()
{
    if (!ensure_track_dir())
    {
        return false;
    }

    if (s_recording)
    {
        return true;
    }

    const std::filesystem::path path = track_dir_path() / make_track_file_name();
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        return false;
    }
    switch (s_format)
    {
    case Format::CSV:
        stream << "epoch_s,lat,lon,alt_m,speed_mps,course_deg\n";
        break;
    case Format::Binary:
    {
        constexpr char kHeader[] = "TMTK1";
        stream.write(kHeader, sizeof(kHeader) - 1);
        break;
    }
    case Format::GPX:
    default:
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        stream << "<gpx version=\"1.1\" creator=\"Trail Mate Linux\">\n";
        stream << "<trk><name>Trail Mate Track</name><trkseg>\n";
        break;
    }
    stream.close();

    s_current_path = path.string();
    s_recording = true;
    s_last_sample_ms = 0;
    s_has_last_point = false;
    return true;
}

void stop_recording()
{
    if (s_recording && s_format == Format::GPX && !s_current_path.empty())
    {
        std::ofstream stream(s_current_path, std::ios::binary | std::ios::app);
        if (stream.is_open())
        {
            stream << "</trkseg></trk></gpx>\n";
        }
    }
    s_recording = false;
    s_started_automatically = false;
    s_current_path.clear();
    s_last_sample_ms = 0;
    s_has_last_point = false;
}

void poll()
{
    if (s_auto_recording && !s_recording)
    {
        s_started_automatically = start_recording();
    }

    const uint32_t now_ms = sys::millis_now();
    if (!should_sample_now(now_ms))
    {
        return;
    }

    const auto state = ::platform::ui::gps::get_data();
    if (append_track_point(state))
    {
        s_last_sample_ms = now_ms;
    }
}

bool current_path(std::string& out_path)
{
    out_path = s_current_path;
    return !out_path.empty();
}

bool list_tracks(std::vector<std::string>& out_tracks, std::size_t max_count)
{
    out_tracks.clear();
    if (!ensure_track_dir())
    {
        return false;
    }

    std::error_code ec;
    for (std::filesystem::directory_iterator it(track_dir_path(), ec), end;
         !ec && it != end; it.increment(ec))
    {
        if (ec) break;
        if (!it->is_regular_file(ec) || ec) continue;
        out_tracks.push_back(it->path().filename().string());
    }

    std::sort(out_tracks.begin(), out_tracks.end(), std::greater<std::string>());
    if (out_tracks.size() > max_count)
    {
        out_tracks.resize(max_count);
    }
    return true;
}

bool remove_track(const std::string& path)
{
    // Containment check against the tracks directory.
    std::filesystem::path resolved;
    if (!::platform::linux_runtime::resolve_child_under_root(
            track_dir_path(), path, resolved))
    {
        return false;
    }

    std::error_code ec;
    return std::filesystem::remove(resolved, ec) && !ec;
}

const char* track_dir()
{
    if (s_track_dir_cache.empty())
    {
        (void)ensure_track_dir();
    }
    return s_track_dir_cache.c_str();
}

void set_auto_recording(bool enabled)
{
    s_auto_recording = enabled;
    if (!enabled && s_started_automatically)
    {
        stop_recording();
    }
}
void set_interval_seconds(uint32_t seconds) { s_interval_seconds = seconds; }
void set_distance_only(bool enabled) { s_distance_only = enabled; }
void set_format(Format format) { s_format = format; }

} // namespace platform::ui::tracker
