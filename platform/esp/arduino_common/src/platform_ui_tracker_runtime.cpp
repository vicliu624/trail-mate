#include "platform/ui/tracker_runtime.h"

#include "platform/esp/arduino_common/gps/track_recorder.h"
#include "platform/ui/device_runtime.h"

#include <SD.h>

namespace platform::ui::tracker
{

bool is_supported()
{
    return true;
}

bool is_recording()
{
    return ::gps::TrackRecorder::getInstance().isRecording();
}

bool start_recording()
{
    return ::gps::TrackRecorder::getInstance().start();
}

void stop_recording()
{
    ::gps::TrackRecorder::getInstance().stop();
}

bool current_path(std::string& out_path)
{
    const String& path = ::gps::TrackRecorder::getInstance().currentPath();
    if (path.isEmpty())
    {
        out_path.clear();
        return false;
    }
    out_path = path.c_str();
    return true;
}

bool list_tracks(std::vector<std::string>& out_tracks, std::size_t max_count)
{
    out_tracks.clear();
    if (max_count == 0)
    {
        return false;
    }

    std::vector<String> names(max_count);
    const std::size_t count = ::gps::TrackRecorder::getInstance().listTracks(names.data(), max_count);
    out_tracks.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        out_tracks.emplace_back(names[index].c_str());
    }
    return !out_tracks.empty();
}

bool remove_track(const std::string& path)
{
    if (!platform::ui::device::sd_ready() || path.empty())
    {
        return false;
    }
    return SD.remove(path.c_str());
}

const char* track_dir()
{
    return ::gps::TrackRecorder::kTrackDir;
}

void set_auto_recording(bool enabled)
{
    ::gps::TrackRecorder::getInstance().setAutoRecording(enabled);
}

void set_interval_seconds(uint32_t seconds)
{
    ::gps::TrackRecorder::getInstance().setIntervalSeconds(seconds);
}

void set_distance_only(bool enabled)
{
    ::gps::TrackRecorder::getInstance().setDistanceOnly(enabled);
}

void set_format(Format format)
{
    ::gps::TrackRecorder::getInstance().setFormat(static_cast<::gps::TrackFormat>(format));
}

} // namespace platform::ui::tracker