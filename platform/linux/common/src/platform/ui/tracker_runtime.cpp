#include "platform/ui/tracker_runtime.h"
#include "gps/gpx/ostream_sink.h"
#include "gps/gpx/track_writer.h"
#include "platform/linux/hostlink_upload_io.h"
#include <ctime>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
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
std::string s_current_logical{};
bool s_auto_recording = false;
bool s_started_automatically = false;
uint32_t s_interval_seconds = 60;
bool s_distance_only = false;
Format s_format = Format::GPX;
Format s_recording_format = Format::GPX;
std::unique_ptr<::platform::linux_runtime::UploadIo> s_recording_io;
uint64_t s_append_offset = 0;
constexpr const char* kGpxFooter = "</trkseg></trk></gpx>\n";
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
    if (!std::filesystem::is_directory(std::filesystem::symlink_status(dir, ec)) || ec) return false;
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
    static uint64_t sequence = 0; // Recording commands are owned by the UI loop.
    std::ostringstream name;
    name.imbue(std::locale::classic());
    name << "track-" << sys::epoch_seconds_now() << '-' << std::hex << std::setw(8) << std::setfill('0') << ::getpid()
         << '-' << std::setw(16) << ++sequence << extension_for_format(s_recording_format);
    return name.str();
}

bool should_sample_now(uint32_t now_ms)
{
    if (!s_recording || s_current_path.empty())
    {
        return false;
    }
    if (!s_has_last_point)
    {
        return true;
    }
    const auto requested = std::max<uint64_t>(1, s_interval_seconds) * 1000;
    const auto interval_ms = uint32_t(std::min<uint64_t>(requested, UINT32_MAX / 2));
    return uint32_t(now_ms - s_last_sample_ms) >= interval_ms;
}

bool distance_gate_allows(const platform::ui::gps::GpsState& state)
{
    if (!s_distance_only || !s_has_last_point)
    {
        return true;
    }
    constexpr double radians = 0.017453292519943295;
    const double dlat = (state.lat - s_last_lat) * radians, dlon = (state.lng - s_last_lng) * radians;
    const double a = std::clamp(std::sin(dlat / 2) * std::sin(dlat / 2) + std::cos(s_last_lat * radians) * std::cos(state.lat * radians) * std::sin(dlon / 2) * std::sin(dlon / 2), 0.0, 1.0);
    return 2.0 * 6371000.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a)) >= 5.0; // Formal IDF distance-only threshold.
}
bool track_name_supported(const std::filesystem::path& path)
{
    auto name = path.filename().string();
    if (name.empty() || name.front() == '.') return false;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c)
                   { return char(std::tolower(c)); });
    if (name == "active.bin") return false;
    const auto extension = std::filesystem::path(name).extension().string();
    return extension == ".gpx" || extension == ".csv" || extension == ".bin";
}

void format_utc(uint32_t epoch_s, char* output, size_t capacity)
{
    output[0] = 0;
    if (epoch_s < 1577836800U || uint64_t(epoch_s) > uint64_t(std::numeric_limits<std::time_t>::max())) return;
    const auto seconds = static_cast<std::time_t>(epoch_s);
    std::tm utc{};
    if (::gmtime_r(&seconds, &utc)) std::strftime(output, capacity, "%Y-%m-%dT%H:%M:%SZ", &utc);
}
void append_csv_point(std::ostream& stream,
                      const platform::ui::gps::GpsState& state,
                      uint32_t epoch_s)
{
    char iso[32]{};
    format_utc(epoch_s, iso, sizeof(iso));
    stream << iso << ','
           << std::fixed << std::setprecision(7) << state.lat << ','
           << std::fixed << std::setprecision(7) << state.lng << ','
           << std::fixed << std::setprecision(2) << (state.has_alt ? state.alt_m : 0.0) << ','
           << std::fixed << std::setprecision(2) << (state.has_speed ? state.speed_mps : 0.0) << ',' << unsigned(state.satellites) << '\n';
}

void append_gpx_point(std::ostream& stream,
                      const platform::ui::gps::GpsState& state,
                      uint32_t epoch_s)
{
    char iso[32]{};
    format_utc(epoch_s, iso, sizeof(iso));
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

void append_binary_point(std::ostream& stream,
                         const platform::ui::gps::GpsState& state,
                         uint32_t epoch_s)
{
    // Formal IDF TMTRK1 layout: explicit LE20 bytes, including zero padding.
    // Native double/struct layouts differ across Linux architectures.
    uint8_t point[20]{};
    const auto u32 = [&](unsigned offset, uint32_t value)
    { for (unsigned i=0;i<4;++i) point[offset+i]=uint8_t(value>>(i*8)); };
    const auto u16 = [&](unsigned offset, uint16_t value)
    { point[offset]=uint8_t(value); point[offset+1]=uint8_t(value>>8); };
    u32(0, epoch_s);
    u32(4, uint32_t(int32_t(std::lround(state.lat * 1e7))));
    u32(8, uint32_t(int32_t(std::lround(state.lng * 1e7))));
    const bool has_alt = state.has_alt && state.alt_m >= -32768 && state.alt_m <= 32767;
    const bool has_speed = state.has_speed && state.speed_mps >= 0 && state.speed_mps <= 655.35;
    const auto altitude = has_alt ? int16_t(std::lround(state.alt_m)) : int16_t(0);
    const auto speed = has_speed ? uint16_t(std::lround(state.speed_mps * 100.0)) : uint16_t(0);
    u16(12, uint16_t(altitude));
    u16(14, speed);
    point[16] = state.satellites;
    point[17] = uint8_t((has_alt ? 1 : 0) | (has_speed ? 2 : 0));
    stream.write(reinterpret_cast<const char*>(point), sizeof(point));
}

bool append_track_point(const platform::ui::gps::GpsState& state)
{
    if (!state.valid || !std::isfinite(state.lat) || !std::isfinite(state.lng) || std::abs(state.lat) > 90 || std::abs(state.lng) > 180 ||
        s_current_path.empty() || !s_recording_io)
    {
        return false;
    }
    if (!distance_gate_allows(state))
    {
        return false;
    }
    if (!s_recording_io->still_named(s_current_logical.c_str()))
    {
        stop_recording();
        return false;
    }

    auto point = state;
    point.has_alt = state.has_alt && std::isfinite(state.alt_m);
    point.has_speed = state.has_speed && std::isfinite(state.speed_mps) && state.speed_mps >= 0;
    point.has_course = state.has_course && std::isfinite(state.course_deg) && state.course_deg >= 0 && state.course_deg <= 360;
    std::ostringstream stream(std::ios::binary | std::ios::out);
    stream.imbue(std::locale::classic());

    const uint32_t epoch_s = sys::epoch_seconds_now();
    switch (s_recording_format)
    {
    case Format::CSV:
        append_csv_point(stream, point, epoch_s);
        break;
    case Format::Binary:
        append_binary_point(stream, point, epoch_s);
        break;
    case Format::GPX:
    default:
        append_gpx_point(stream, point, epoch_s);
        break;
    }

    if (!stream.good()) return false;
    const auto encoded = stream.str();
    const auto payload = encoded + (s_recording_format == Format::GPX ? kGpxFooter : "");
    if (!s_recording_io->seek(s_recording_io.get(), s_append_offset) ||
        s_recording_io->write(s_recording_io.get(), reinterpret_cast<const uint8_t*>(payload.data()), payload.size()) != payload.size() ||
        !s_recording_io->sync(s_recording_io.get()))
    {
        const auto footer = s_recording_format == Format::GPX ? std::string(kGpxFooter) : std::string{};
        if (s_recording_io->current() && s_recording_io->still_named(s_current_logical.c_str()))
        {
            (void)s_recording_io->seek(s_recording_io.get(), s_append_offset);
            if (!footer.empty()) (void)s_recording_io->write(s_recording_io.get(), reinterpret_cast<const uint8_t*>(footer.data()), footer.size());
            (void)s_recording_io->truncate(s_recording_io.get(), s_append_offset + footer.size());
        }
        stop_recording();
        return false;
    }
    s_append_offset += encoded.size();
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

    s_recording_format = s_format == Format::CSV || s_format == Format::Binary ? s_format : Format::GPX;
    auto io = std::make_unique<::platform::linux_runtime::UploadIo>(::platform::linux_runtime::resolve_paths().sd_root);
    std::string name, logical;
    bool created = false;
    for (unsigned retry = 0; retry < 64 && !created; ++retry)
    {
        name = make_track_file_name();
        logical = "/tracks/" + name;
        created = io->open(logical.c_str(), ::platform::linux_runtime::UploadIo::Mode::Exclusive).handle != nullptr;
    }
    if (!created) return false;
    if (!io->lock_open(io.get()))
    {
        io->close(io.get());
        io->remove(logical.c_str());
        return false;
    }
    std::string header;
    switch (s_recording_format)
    {
    case Format::CSV:
        header = "time,lat,lon,alt_m,speed_mps,satellites\n";
        break;
    case Format::Binary:
    {
        header = "TMTRK1";
        break;
    }
    case Format::GPX:
    default:
        header = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gpx version=\"1.1\" creator=\"Trail Mate Linux\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n<trk><name>Trail Mate Track</name><trkseg>\n";
        break;
    }
    const auto payload = header + (s_recording_format == Format::GPX ? kGpxFooter : "");
    if (io->write(io.get(), reinterpret_cast<const uint8_t*>(payload.data()), payload.size()) != payload.size() || !io->sync(io.get()))
    {
        io->close(io.get());
        io->remove(logical.c_str());
        return false;
    }
    s_append_offset = header.size();
    s_recording_io = std::move(io);
    s_current_logical = logical;
    s_current_path = (track_dir_path() / name).string();
    s_recording = true;
    s_last_sample_ms = 0;
    s_has_last_point = false;
    return true;
}

void stop_recording()
{
    if (s_recording_io)
    {
        (void)s_recording_io->sync(s_recording_io.get());
        s_recording_io.reset();
    }
    s_recording = false;
    s_started_automatically = false;
    s_current_path.clear();
    s_current_logical.clear();
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
         !ec && it != end && out_tracks.size() < max_count; it.increment(ec))
    {
        if (ec) break;
        if (it->is_symlink(ec) || ec || !it->is_regular_file(ec) || ec) continue;
        if (it->hard_link_count(ec) != 1 || ec) continue;
        if (!track_name_supported(it->path())) continue;
        out_tracks.push_back(it->path().filename().string());
    }

    std::sort(out_tracks.begin(), out_tracks.end(), std::greater<std::string>());
    if (out_tracks.size() > max_count)
    {
        out_tracks.resize(max_count);
    }
    return !ec;
}

bool remove_track(const std::string& path)
{
    const std::filesystem::path input(path);
    const auto relative = input.is_absolute() ? input.lexically_relative(track_dir_path()) : input;
    if (relative.empty() || !relative.parent_path().empty()) return false;
    if (!track_name_supported(relative)) return false;
    if (s_recording && relative.filename() == std::filesystem::path(s_current_path).filename()) return false;
    ::platform::linux_runtime::UploadIo io(::platform::linux_runtime::resolve_paths().sd_root);
    const auto logical = "/tracks/" + relative.generic_string();
    const auto file = io.open(logical.c_str(), ::platform::linux_runtime::UploadIo::Mode::Read);
    if (!file.handle || !io.lock_open(file.handle)) return false;
    return io.remove(logical.c_str()); // Keep the file lock until unlink completes.
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
