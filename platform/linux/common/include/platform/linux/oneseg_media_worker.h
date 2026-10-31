#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace platform::linux_runtime
{
class OneSegMediaAudio
{
  public:
    virtual ~OneSegMediaAudio() = default;
    virtual bool begin(uint64_t generation) = 0; // Mono signed PCM16 at 48 kHz.
    virtual bool write(const int16_t*, size_t samples, int64_t pts_us, uint64_t generation,
                       const std::atomic_bool& cancel) = 0;
    virtual bool end(bool cancelled) = 0;
    virtual const char* lastError() const { return ""; }
};

// A worker-owned native FFmpeg decoder with bounded TS and RGB565 storage.
// Configuration PIDs must come from received PMT metadata. No hardware/RF or
// "Playing" state is inferred from packets or decoded frames.
class OneSegMediaWorker
{
  public:
    static constexpr size_t max_width = 640, max_height = 480;
    struct Config
    {
        uint64_t generation = 0;
        uint16_t video_pid = 0x1fff, audio_pid = 0x1fff;
        uint16_t width = 320, height = 180;
    };
    enum class Phase : uint8_t
    {
        Idle,
        Opening,
        Decoding,
        Stopped,
        Error
    };
    enum class Error : uint8_t
    {
        None,
        Configuration,
        QueueFull,
        Input,
        Demux,
        Codec,
        Video,
        Audio,
        Internal
    };
    struct Snapshot
    {
        Phase phase = Phase::Idle;
        Error error = Error::None;
        uint64_t generation = 0, ts_packets = 0, video_frames = 0, audio_frames = 0, pcm_samples = 0;
        uint64_t decode_errors = 0, superseded_frames = 0;
        uint32_t queue_high_water = 0;
        char message[160]{};
    };
    struct FrameInfo
    {
        uint64_t generation = 0, ordinal = 0;
        int64_t pts_us = INT64_MIN;
        uint16_t width = 0, height = 0;
        uint32_t stride = 0;
    };
    explicit OneSegMediaWorker(OneSegMediaAudio* audio);
    ~OneSegMediaWorker();
    // Owner thread only, after joining the previous generation. Does not probe
    // streams, open audio, or wait for data on the calling/UI thread.
    bool start(Config config);
    // One TS producer only: bounded copy plus lock-free atomics, no waits/I/O.
    bool acceptTs(const uint8_t*, size_t bytes, uint64_t generation);
    void endInput();
    void requestStop();
    void join(); // Worker/shutdown/test only, never LVGL.
    bool healthy() const;
    Snapshot snapshot() const;
    // Single UI reader; copies the latest RGB565LE frame without waiting for
    // decoding. Frame timestamps and generation must be checked by presentation.
    bool copyLatestFrame(uint64_t generation, uint8_t* destination, size_t capacity, FrameInfo& info);
    // Pick the newest queued frame no more than 30ms early / 250ms late.
    // Unknown timestamps are ineligible. Selection happens before copying so
    // a rejected future frame cannot overwrite an LVGL-owned display buffer.
    bool copyFrameAt(uint64_t generation, int64_t pts_us, uint8_t* destination, size_t capacity, FrameInfo& info);
    bool latestFrameInfo(uint64_t generation, FrameInfo& info) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
