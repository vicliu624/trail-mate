#include "platform/ui/tracker_runtime.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace platform::ui::tracker
{
namespace
{

constexpr const char* kSdRootEnv = "TRAIL_MATE_SD_ROOT";
constexpr const char* kSettingsRootEnv = "TRAIL_MATE_SETTINGS_ROOT";

bool s_recording = false;
std::string s_current_path{};
bool s_auto_recording = false;
uint32_t s_interval_seconds = 60;
bool s_distance_only = false;
Format s_format = Format::GPX;
std::string s_track_dir_cache{};

std::filesystem::path default_storage_root()
{
    if (const char* configured = std::getenv(kSdRootEnv))
    {
        if (configured[0] != '\0')
        {
            return std::filesystem::path(configured);
        }
    }

    if (const char* configured = std::getenv(kSettingsRootEnv))
    {
        if (configured[0] != '\0')
        {
            return std::filesystem::path(configured) / "sdcard";
        }
    }

#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"))
    {
        if (appdata[0] != '\0')
        {
            return std::filesystem::path(appdata) / "TrailMateCardputerZero" / "sdcard";
        }
    }
#endif

    if (const char* home = std::getenv("HOME"))
    {
        if (home[0] != '\0')
        {
            return std::filesystem::path(home) / ".trailmate_cardputer_zero" / "sdcard";
        }
    }

    return std::filesystem::current_path() / ".trailmate_cardputer_zero" / "sdcard";
}

std::filesystem::path track_dir_path()
{
    return default_storage_root() / "tracks";
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
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    return "track-" + std::to_string(seconds) + extension_for_format(s_format);
}

std::filesystem::path resolve_track_path(const std::string& path_or_name)
{
    std::filesystem::path candidate(path_or_name);
    if (candidate.is_absolute())
    {
        return candidate;
    }
    return track_dir_path() / candidate;
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
    stream << "# Trail Mate Linux tracker\n";
    stream << "# auto_recording=" << (s_auto_recording ? "1" : "0") << "\n";
    stream << "# interval_seconds=" << s_interval_seconds << "\n";
    stream << "# distance_only=" << (s_distance_only ? "1" : "0") << "\n";
    stream.close();

    s_current_path = path.string();
    s_recording = true;
    return true;
}

void stop_recording()
{
    s_recording = false;
    s_current_path.clear();
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
    for (std::filesystem::directory_iterator it(track_dir_path(), ec), end; !ec && it != end;
         it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        if (!it->is_regular_file(ec) || ec)
        {
            continue;
        }
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
    std::error_code ec;
    const std::filesystem::path resolved = resolve_track_path(path);
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
}

void set_interval_seconds(uint32_t seconds)
{
    s_interval_seconds = seconds;
}

void set_distance_only(bool enabled)
{
    s_distance_only = enabled;
}

void set_format(Format format)
{
    s_format = format;
}

} // namespace platform::ui::tracker
