#include "platform/ui/tracker_runtime.h"

#include "platform/esp/idf_common/bsp_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>

namespace platform::ui::tracker
{
namespace
{

bool is_regular_file(const std::string& path)
{
    struct stat st
    {
    };
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

} // namespace

bool is_supported()
{
    return false;
}

bool is_recording()
{
    return false;
}

bool start_recording()
{
    return false;
}

void stop_recording() {}

bool current_path(std::string& out_path)
{
    out_path.clear();
    return false;
}

bool list_tracks(std::vector<std::string>& out_tracks, std::size_t max_count)
{
    out_tracks.clear();
    if (max_count == 0 || !platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
    {
        return false;
    }

    const std::string base = std::string(platform::esp::idf_common::bsp_runtime::sdcard_mount_point()) + track_dir();
    DIR* dir = ::opendir(base.c_str());
    if (dir == nullptr)
    {
        return false;
    }

    while (out_tracks.size() < max_count)
    {
        dirent* entry = ::readdir(dir);
        if (entry == nullptr)
        {
            break;
        }
        const char* name_c = entry->d_name;
        if (name_c == nullptr || std::strcmp(name_c, ".") == 0 || std::strcmp(name_c, "..") == 0)
        {
            continue;
        }
        std::string name = name_c;
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
    ::closedir(dir);
    std::sort(out_tracks.begin(), out_tracks.end());
    return !out_tracks.empty();
}

bool remove_track(const std::string& path)
{
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready() || path.empty())
    {
        return false;
    }
    const std::string mount_prefixed = std::string(platform::esp::idf_common::bsp_runtime::sdcard_mount_point()) + path;
    return std::remove(mount_prefixed.c_str()) == 0;
}

const char* track_dir()
{
    return "/trackers";
}

void set_auto_recording(bool enabled)
{
    (void)enabled;
}

void set_interval_seconds(uint32_t seconds)
{
    (void)seconds;
}

void set_distance_only(bool enabled)
{
    (void)enabled;
}

void set_format(Format format)
{
    (void)format;
}

} // namespace platform::ui::tracker