#pragma once
#include <cstdint>

namespace platform::ui::fm
{
enum class Phase : uint8_t
{
    Idle,
    Starting,
    PcmSubmitted,
    Stopping,
    Stopped,
    Error
};
enum class Error : uint8_t
{
    None,
    RadioBusy,
    Audio,
    Memory,
    Task,
    InvalidIq,
    QueueFull,
    Stream,
    Restore,
    Cleanup
};
struct Snapshot
{
    Phase phase = Phase::Idle;
    Error error = Error::None;
    uint32_t frequency_hz = 100800000;
    uint64_t session = 0, processed_pairs = 0, pcm_samples = 0;
    uint8_t volume = 50;
    bool active = false, calibration_exception_requested = false;
    bool cleanup_failed = false, chip_flags_valid = false;
    uint16_t chip_flags = 0;
    int capture_error = 0, recovery_error = 0;
};
#if (defined(ESP_PLATFORM) && defined(TRAIL_MATE_IDF_RADIO_LR2021)) || defined(TRAIL_MATE_EXTERNAL_FM_RUNTIME)
bool is_supported();
// UI-safe requests. Start rejects active/unclean sessions; stop also retries a
// failed audio cleanup. All hardware I/O and task joins run off the UI thread.
bool start(uint32_t frequency_hz = 100800000, bool allow_image_calibration_warning = false);
void request_stop();
void set_volume(unsigned percent);
Snapshot snapshot();
void wait_until_finished(); // Shutdown/tests only, never a page callback.
#else
inline bool is_supported()
{
    return false;
}
inline bool start(uint32_t = 100800000, bool = false) { return false; }
inline void request_stop() {}
inline void set_volume(unsigned) {}
inline Snapshot snapshot() { return {}; }
inline void wait_until_finished() {}
#endif
} // namespace platform::ui::fm
