#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace platform::ui::tracker
{

enum class Format : uint8_t
{
    GPX = 0,
    CSV = 1,
    Binary = 2,
};

bool is_supported();
bool is_recording();
bool start_recording();
void stop_recording();
bool current_path(std::string& out_path);
bool list_tracks(std::vector<std::string>& out_tracks, std::size_t max_count = 32);
bool remove_track(const std::string& path);
const char* track_dir();
void set_auto_recording(bool enabled);
void set_interval_seconds(uint32_t seconds);
void set_distance_only(bool enabled);
void set_format(Format format);

} // namespace platform::ui::tracker
