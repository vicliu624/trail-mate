#pragma once
#include "platform/linux/packet_radio.h"
#include "platform/ui/walkie_runtime.h"
#include <memory>

namespace platform::linux_runtime
{
class IWalkieAudio
{
  public:
    virtual ~IWalkieAudio() = default;
    virtual bool startCapture() = 0;
    virtual void stopCapture() = 0;
    virtual std::size_t capture(int16_t* output, std::size_t count) = 0;
    virtual uint64_t captureDropped() const = 0;
    virtual bool captureActive() const = 0;
    virtual bool play(const int16_t* samples, std::size_t count) = 0;
    virtual bool playbackReady() const = 0;
    virtual void stopPlayback() = 0;
    virtual const char* lastError() const = 0;
};
struct WalkieOptions
{
    RadioSettings radio;
    uint32_t self_id = 0;
    uint16_t session_seed = 0;
};
// One worker owns the session. Ports are injected so codec/wire/ownership are
// exercised without a second physical radio or microphone.
class WalkieSession
{
  public:
    WalkieSession(IPacketRadio& radio, IWalkieAudio& audio);
    ~WalkieSession();
    bool begin(const WalkieOptions& options);
    bool step(bool ptt, uint32_t now_ms, uint8_t volume);
    void end();
    ::platform::ui::walkie::Status status() const;
    const char* lastError() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
