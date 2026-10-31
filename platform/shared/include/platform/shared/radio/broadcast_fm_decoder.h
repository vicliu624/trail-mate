#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace platform::shared::radio
{
// Mono broadcast-FM DSP only. The caller must supply channel-centred 250 kS/s
// complex IQ and reset on retunes, sample loss or generation changes. This is
// not an IQ acquisition backend or proof that a radio covers the FM band.
// Keep this object in owner storage, not on an ESP task's automatic stack.
class BroadcastFmDecoder
{
  public:
    static constexpr uint32_t kInputRate = 250000;
    static constexpr uint32_t kOutputRate = 48000;
    static constexpr size_t kFilterTaps = 127;

    enum class Deemphasis
    {
        Us50,
        Us75
    };

    explicit BroadcastFmDecoder(Deemphasis emphasis = Deemphasis::Us50)
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double cutoff = 15000.0 / kInputRate;
        double sum = 0;
        for (size_t n = 0; n < kFilterTaps; ++n)
        {
            const double x = static_cast<double>(n) - (kFilterTaps - 1) / 2.0;
            const double sinc = x == 0 ? 2 * cutoff : std::sin(2 * pi * cutoff * x) / (pi * x);
            const double window = 0.42 - 0.5 * std::cos(2 * pi * n / (kFilterTaps - 1)) +
                                  0.08 * std::cos(4 * pi * n / (kFilterTaps - 1));
            taps_[n] = static_cast<float>(sinc * window);
            sum += taps_[n];
        }
        for (auto& tap : taps_) tap = static_cast<float>(tap / sum);
        dc_pole_ = static_cast<float>(std::exp(-2 * pi * 30 / kInputRate));
        const double tau = emphasis == Deemphasis::Us50 ? 50e-6 : 75e-6;
        emphasis_alpha_ = static_cast<float>(1 - std::exp(-1.0 / (kInputRate * tau)));
    }

    void reset()
    {
        history_.fill(0);
        position_ = 0;
        phase_ = 0;
        previous_i_ = previous_q_ = 0;
        previous_valid_ = false;
        dc_input_ = dc_output_ = emphasis_output_ = 0;
    }

    // Consumes exactly one IQ pair. Returns true only when pcm was written.
    // No allocation or hidden output queue; the caller owns each emitted sample.
    bool push(int16_t i, int16_t q, int16_t& pcm)
    {
        constexpr float scale = static_cast<float>(kInputRate / (2.0 * 3.14159265358979323846 * 75000.0));
        float demodulated = 0;
        const bool valid = i != 0 || q != 0;
        if (valid && previous_valid_)
        {
            const double cross = static_cast<double>(previous_i_) * q - static_cast<double>(previous_q_) * i;
            const double dot = static_cast<double>(previous_i_) * i + static_cast<double>(previous_q_) * q;
            demodulated = static_cast<float>(std::atan2(cross, dot)) * scale;
        }
        previous_i_ = i;
        previous_q_ = q;
        previous_valid_ = valid;

        const float dc = demodulated - dc_input_ + dc_pole_ * dc_output_;
        dc_input_ = demodulated;
        dc_output_ = dc;
        emphasis_output_ += emphasis_alpha_ * (dc - emphasis_output_);
        history_[position_] = emphasis_output_;

        // Calculate the FIR only for output instants.
        // Linear interpolation of adjacent filtered samples locates the exact
        // 48 kHz instant between 250 kHz input samples without clock drift.
        phase_ += kOutputRate;
        const bool emit = phase_ >= kInputRate;
        if (emit)
        {
            phase_ -= kInputRate;
            float current = 0;
            float previous = 0;
            size_t index = position_;
            size_t older = position_ == 0 ? kFilterTaps - 1 : position_ - 1;
            for (size_t n = 0; n < kFilterTaps; ++n)
            {
                current += taps_[n] * history_[index];
                // The oldest value has already been overwritten. A zero end
                // tap in this Blackman FIR makes that term immaterial.
                previous += taps_[n] * history_[older];
                index = index == 0 ? kFilterTaps - 1 : index - 1;
                older = older == 0 ? kFilterTaps - 1 : older - 1;
            }
            const float fraction = static_cast<float>(phase_) / kOutputRate;
            const float value = current + fraction * (previous - current);
            const float bounded = value > 1 ? 1 : (value < -1 ? -1 : value);
            pcm = static_cast<int16_t>(std::lround(bounded * 32767));
        }
        position_ = (position_ + 1) % kFilterTaps;
        return emit;
    }

  private:
    std::array<float, kFilterTaps> taps_{};
    std::array<float, kFilterTaps> history_{};
    size_t position_ = 0;
    uint32_t phase_ = 0;
    int16_t previous_i_ = 0;
    int16_t previous_q_ = 0;
    bool previous_valid_ = false;
    float dc_pole_ = 0;
    float emphasis_alpha_ = 0;
    float dc_input_ = 0;
    float dc_output_ = 0;
    float emphasis_output_ = 0;
};
} // namespace platform::shared::radio
