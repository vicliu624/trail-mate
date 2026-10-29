#pragma once
#include "platform/linux/call_audio_session.h"
#include <functional>

namespace platform::linux_runtime
{
struct CallAudioOptions
{
    // Cached, thread-safe facts only; callbacks must not reenter call runtime.
    std::function<bool()> available;
    std::function<std::unique_ptr<ICallPcm>()> make_pcm;
    uint32_t start_timeout_ms = 1500;
    bool ring_alert = true;
};
struct CallAudioSnapshot
{
    bool built = false, installed = false, supported = false, running = false;
    uint32_t started = 0, failed = 0;
    CallAudioStatistics statistics;
};
// Installs the existing call media/realtime ports. It does not advertise a
// transport capability or fabricate a signalling link. Hosts stop protocol/UI
// producers before shutdown; all device work belongs to the owned worker.
class NativeCallAudioBinding
{
  public:
    explicit NativeCallAudioBinding(CallAudioOptions options = {});
    ~NativeCallAudioBinding();
    NativeCallAudioBinding(const NativeCallAudioBinding&) = delete;
    NativeCallAudioBinding& operator=(const NativeCallAudioBinding&) = delete;
    CallAudioSnapshot snapshot() const;
    void shutdown();

  private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
