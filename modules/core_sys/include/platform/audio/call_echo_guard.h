#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace platform::audio
{
// Render-driven capture gating, not an adaptive acoustic echo canceller.
class CallEchoGuard
{
  public:
    explicit CallEchoGuard(uint32_t rate, uint16_t threshold = 384, uint16_t hold_ms = 160)
        : threshold_(threshold), hold_(uint32_t(std::min<uint64_t>(uint64_t(rate) * hold_ms / 1000, UINT32_MAX))) {}
    bool observeRender(const int16_t* pcm, size_t samples)
    {
        if (!pcm || !samples) return false;
        uint64_t sum = 0;
        for (size_t index = 0; index < samples; ++index)
        {
            const int32_t sample = pcm[index];
            sum += uint32_t(sample < 0 ? -sample : sample);
        }
        mean_ = uint16_t(std::min<uint64_t>(sum / samples, UINT16_MAX));
        const bool before = captureSuppressed();
        if (mean_ >= threshold_) remaining_ = hold_;
        else if (remaining_ > samples) remaining_ -= uint32_t(samples);
        else remaining_ = 0;
        return before != captureSuppressed();
    }
    bool suppressCapture(int16_t* pcm, size_t samples) const
    {
        if (!captureSuppressed() || !pcm || !samples) return false;
        std::memset(pcm, 0, samples * sizeof(int16_t));
        return true;
    }
    bool captureSuppressed() const { return remaining_ != 0; }
    uint16_t lastRenderMeanAbs() const { return mean_; }

  private:
    uint16_t threshold_ = 0;
    uint32_t hold_ = 0, remaining_ = 0;
    uint16_t mean_ = 0;
};
} // namespace platform::audio
