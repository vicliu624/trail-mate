#include "platform/esp/common/call_acoustic_echo_guard.h"

namespace platform::esp::common::reticulum_call_audio
{

CallAcousticEchoGuard::CallAcousticEchoGuard(uint32_t sample_rate_hz,
                                             uint16_t render_active_mean_abs,
                                             uint16_t release_hold_ms)
    : guard_(sample_rate_hz, render_active_mean_abs, release_hold_ms) {}

bool CallAcousticEchoGuard::observeRender(const int16_t* pcm,
                                          std::size_t sample_count)
{
    return guard_.observeRender(pcm, sample_count);
}

bool CallAcousticEchoGuard::suppressCapture(int16_t* pcm,
                                            std::size_t sample_count) const
{
    return guard_.suppressCapture(pcm, sample_count);
}

bool CallAcousticEchoGuard::captureSuppressed() const
{
    return guard_.captureSuppressed();
}

uint16_t CallAcousticEchoGuard::lastRenderMeanAbs() const
{
    return guard_.lastRenderMeanAbs();
}

} // namespace platform::esp::common::reticulum_call_audio
