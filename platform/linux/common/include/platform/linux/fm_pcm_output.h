#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace platform::linux_runtime
{
// One DSP worker owns lifecycle/write. Only setVolume may run concurrently.
// Acceptance means queued PCM, never station lock or audible playback proof.
class FmPcmOutput
{
  public:
    FmPcmOutput();
    ~FmPcmOutput();
    bool begin(uint64_t session);
    bool write(const int16_t*, size_t, uint64_t session, const std::atomic_bool& cancel);
    void end() noexcept;
    void setVolume(unsigned percent);
    const char* lastError() const;
    FmPcmOutput(const FmPcmOutput&) = delete;
    FmPcmOutput& operator=(const FmPcmOutput&) = delete;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
