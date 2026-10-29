#pragma once

#include "platform/audio/call_echo_guard.h"
#include <cstddef>
#include <cstdint>

namespace platform::esp::common::reticulum_call_audio
{

class CallAcousticEchoGuard
{
  public:
    explicit CallAcousticEchoGuard(uint32_t sample_rate_hz,
                                   uint16_t render_active_mean_abs = 384,
                                   uint16_t release_hold_ms = 160);

    bool observeRender(const int16_t* pcm, std::size_t sample_count);
    bool suppressCapture(int16_t* pcm, std::size_t sample_count) const;

    bool captureSuppressed() const;
    uint16_t lastRenderMeanAbs() const;

  private:
    ::platform::audio::CallEchoGuard guard_;
};

} // namespace platform::esp::common::reticulum_call_audio
