#pragma once
#include "platform/linux/oneseg_audio_clock.h"
#include "platform/linux/oneseg_media_worker.h"

namespace platform::linux_runtime
{
// Physical Linux PCM adapter. Lifecycle/write are media-worker operations;
// setVolume is an atomic UI request. Takes exclusive Television audio focus.
class OneSegPcmOutput final : public OneSegMediaAudio
{
  public:
    OneSegPcmOutput();
    ~OneSegPcmOutput() override;
    bool begin(uint64_t generation) override;
    bool write(const int16_t*, size_t, int64_t pts_us, uint64_t generation,
               const std::atomic_bool& cancel) override;
    bool end(bool cancelled) override;
    const char* lastError() const override;
    void setVolume(unsigned percent);
    OneSegAudioClock::Snapshot clock() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
