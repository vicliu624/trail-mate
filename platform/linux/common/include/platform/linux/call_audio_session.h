#pragma once
#include "platform/ui/reticulum_call_runtime.h"
#include <memory>

namespace platform::linux_runtime
{
class ICallPcm
{
  public:
    virtual ~ICallPcm() = default;
    virtual bool open(uint32_t sample_rate) = 0;
    virtual bool captureActive() const = 0;
    virtual uint64_t captureDropped() const = 0;
    virtual size_t read(int16_t* samples, size_t capacity) = 0;
    virtual void discardCapture() = 0;
    virtual bool writable() const = 0;
    virtual bool write(const int16_t* samples, size_t count) = 0;
    virtual void discardPlayback() = 0;
    virtual void close() noexcept = 0;
};
class ICallAudioPackets
{
  public:
    virtual ~ICallAudioPackets() = default;
    virtual bool receive(::platform::ui::reticulum_call::AudioPacket&) = 0;
    virtual bool transmit(const ::platform::ui::reticulum_call::AudioPacket&) = 0;
};
struct CallAudioStatistics
{
    uint64_t captured_frames = 0, decoded_frames = 0, queued_packets = 0;
    uint64_t rejected_packets = 0, jitter_dropped = 0, queue_dropped = 0;
    uint64_t underruns = 0, echo_suppressed = 0;
};
// One worker owns this session and both ports. The caller reserves audio focus
// before begin and releases it after end. All storage is bounded and reusable.
class CallAudioSession
{
  public:
    CallAudioSession(ICallPcm&, ICallAudioPackets&);
    ~CallAudioSession();
    bool begin(const ::platform::ui::reticulum_call::Snapshot&, uint32_t now_ms);
    bool step(const ::platform::ui::reticulum_call::Snapshot&, uint32_t now_ms, uint8_t volume);
    void end();
    CallAudioStatistics statistics() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
