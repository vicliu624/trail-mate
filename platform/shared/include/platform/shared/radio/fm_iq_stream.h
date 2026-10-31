#pragma once
#include "platform/shared/radio/fm_lr2021_rx.h"

namespace platform::shared::radio
{
struct FmIqRequest
{
    uint32_t frequency_hz = 100800000;
    uint8_t* staging = nullptr;
    size_t staging_bytes = 0;
    void* context = nullptr;
    bool (*accept)(void*, const uint8_t*, size_t, uint64_t first_pair) = nullptr;
    bool (*cancelled)(void*) = nullptr;
    bool allow_image_calibration_warning = false;
};
struct FmIqResult
{
    FmLr2021Rx::Result setup;
    uint64_t validated_bytes = 0, elapsed_us = 0, max_service_gap_us = 0;
    uint32_t nominal_rate_hz = 0, max_fifo_level = 0;
    uint8_t fifo_flags = 0;
    bool cancelled = false, sink_rejected = false, setup_attempted = false;
    int capture_error = 0, recovery_error = 0;
};
} // namespace platform::shared::radio
