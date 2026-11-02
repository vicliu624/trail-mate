#pragma once
#include "platform/linux/oneseg_ofdm_lab.h"

namespace platform::linux_runtime
{
// Actual native block readback. sampling_ppm is state at the first observation,
// not the requested initial clock seed. Availability is not RF/programme lock.
struct OneSegNativeConfiguration
{
    bool available = false;
    OneSegLabSource source = OneSegLabSource::Unavailable;
    uint64_t received_ms = 0;
    double mode = 0, fft_length = 0, cp_length = 0, carriers = 0, sample_rate = 0;
    double alpha = 0, beta = 0, timing_alpha = 0, timing_beta = 0;
    double integer_search = 0, offset_confirmation = 0, channel_average = 0, equalizer_floor = 0, sampling_ppm = 0;
    bool valid() const
    {
        for (double value : {mode, fft_length, cp_length, carriers, sample_rate, alpha, beta, timing_alpha, timing_beta,
                             integer_search, offset_confirmation, channel_average, equalizer_floor, sampling_ppm})
            if (!std::isfinite(value) || std::abs(value) > 1e9) return false;
        for (double value : {mode, fft_length, cp_length, carriers, integer_search, offset_confirmation, channel_average})
            if (value != std::floor(value)) return false;
        if (mode < 1 || mode > 3 || fft_length < 16 || fft_length > 8192 || cp_length < 1 || cp_length > fft_length / 4 + 1 ||
            carriers != 108 * std::pow(2., mode - 1) || sample_rate <= 0 || sample_rate > 2000000) return false;
        for (double coefficient : {alpha, beta, timing_alpha, timing_beta})
            if (coefficient < 0 || coefficient > 4) return false;
        return integer_search >= 1 && integer_search <= 32 && offset_confirmation >= 1 && offset_confirmation <= 64 &&
               channel_average >= 1 && channel_average <= 32 && equalizer_floor >= 0 && equalizer_floor <= 1;
    }
};
} // namespace platform::linux_runtime
