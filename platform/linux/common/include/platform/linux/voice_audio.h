#pragma once
#include "chat/infra/voice/vmp_wire.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace platform::linux_runtime
{
// VMP v1's Codec2-1300 profile, shared with the production Pager peer.
struct VoiceClip
{
    static constexpr size_t samples_per_frame = 320, bytes_per_frame = 7, max_frames = 125;
    std::array<uint8_t, bytes_per_frame * max_frames> bytes{};
    size_t size = 0;
    bool valid() const { return size && size <= bytes.size() && size % bytes_per_frame == 0; }
    unsigned duration_ms() const { return valid() ? unsigned(size / bytes_per_frame) * 40 : 0; }
    void clear();
};
// Worker-owned PCM port. Closing is unconditional, including failed opens.
class IVoicePcm
{
  public:
    virtual ~IVoicePcm() = default;
    virtual bool beginCapture() = 0;
    virtual size_t read(int16_t*, size_t capacity) = 0;
    virtual bool captureActive() const = 0;
    virtual uint64_t dropped() const = 0;
    virtual void endCapture() noexcept = 0;
    virtual bool beginPlayback() = 0;
    virtual bool writable() const = 0;
    virtual bool write(const int16_t*, size_t) = 0;
    virtual bool drain() = 0;
    virtual void endPlayback() noexcept = 0;
};
class NativeVoiceAudio
{
  public:
    enum class Result
    {
        Complete,
        Cancelled,
        Busy,
        InvalidMedia,
        AudioFailure,
        CodecFailure
    };
    explicit NativeVoiceAudio(std::unique_ptr<IVoicePcm> pcm = {});
    ~NativeVoiceAudio();
    // Synchronous worker operations, never called from LVGL. A stop preserves
    // complete frames; cancellation discards the entire capture. Flags and
    // playback media must remain alive until the call returns.
    Result capture(const std::atomic_bool& stop, const std::atomic_bool& cancel);
    const VoiceClip& captured() const;
    Result play(const VoiceClip&, ::chat::voice::vmp::Codec, unsigned volume, const std::atomic_bool& cancel);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
