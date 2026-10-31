#pragma once
#include <cstdint>

namespace platform::ui::radio_iq
{
enum class Phase : uint8_t
{
    Idle,
    Capturing,
    Ready,
    Error
};
struct Snapshot
{
    Phase phase = Phase::Idle;
    uint32_t pairs = 0;
    uint32_t nominal_rate_hz = 0;
    uint32_t clipped_pairs = 0;
    float rms = 0;
    float dc_i = 0, dc_q = 0;
    uint64_t elapsed_us = 0;
    int capture_error = 0, restore_error = 0, recovery_error = 0;
    char error[96]{};
};

#if (defined(ESP_PLATFORM) && defined(TRAIL_MATE_IDF_RADIO_LR2021)) || defined(TRAIL_MATE_EXTERNAL_RADIO_IQ_RUNTIME) || defined(TRAIL_MATE_PLATFORM_LINUX)
bool is_supported();
bool start();
Snapshot snapshot();
// Page exit must join the bounded capture/recovery before another app can
// acquire the radio. The worker never retains LVGL objects or page pointers.
void wait_until_finished();
#else
inline bool is_supported()
{
    return false;
}
inline bool start() { return false; }
inline Snapshot snapshot() { return {}; }
inline void wait_until_finished() {}
#endif
} // namespace platform::ui::radio_iq
