#include "platform/ui/tracker_runtime.h"

namespace platform::ui::tracker
{
namespace
{

constexpr const char* kTrackDir = "/tracks";

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

void stop_recording()
{
}

bool current_path(std::string& out_path)
{
    out_path.clear();
    return false;
}

bool list_tracks(std::vector<std::string>& out_tracks, std::size_t)
{
    out_tracks.clear();
    return false;
}

bool remove_track(const std::string&)
{
    return false;
}

const char* track_dir()
{
    return kTrackDir;
}

void set_auto_recording(bool)
{
}

void set_interval_seconds(uint32_t)
{
}

void set_distance_only(bool)
{
}

void set_format(Format)
{
}

} // namespace platform::ui::tracker
