#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <Arduino.h>
#include <SD.h>

namespace gps
{

struct TrackPoint
{
    double lat = 0;
    double lon = 0;
    uint8_t satellites = 0;
    time_t timestamp = 0;
};

// Simple GPX recorder with append-only writes for SD stability.
class TrackRecorder
{
  public:
    static TrackRecorder& getInstance();

    bool start();
    void stop();
    bool isRecording() const { return recording_; }
    const String& currentPath() const { return current_path_; }

    // Append a single point if recording is active and SD is ready.
    void appendPoint(const TrackPoint& pt);

    // List GPX files under /trackers; returns count.
    size_t listTracks(String* out_names, size_t max_names) const;

    static constexpr const char* kTrackDir = "/trackers";

  private:
    TrackRecorder() = default;
    TrackRecorder(const TrackRecorder&) = delete;
    TrackRecorder& operator=(const TrackRecorder&) = delete;

    bool ensureDir() const;
    String makeTrackPath() const;
    static String isoTime(time_t t);

    SemaphoreHandle_t mutex_ = xSemaphoreCreateMutex();
    bool recording_ = false;
    String current_path_{};
    bool last_point_valid_ = false;
    TrackPoint last_point_{};
};

} // namespace gps
