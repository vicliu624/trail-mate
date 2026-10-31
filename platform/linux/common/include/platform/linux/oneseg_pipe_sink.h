#pragma once

#include "platform/linux/oneseg_receive_worker.h"

namespace platform::linux_runtime
{
// Connects the consumer to a decoder's stdin pipe. Factory returns a fresh
// owned write FD for each generation, or -1; this sink closes every returned
// FD. The factory/decoder controller owns child process cancellation/reaping.
// This class forwards IQ, and never claims TS lock or media playback.
class OneSegPipeSink final : public OneSegIqSink
{
  public:
    struct Factory
    {
        void* context = nullptr;
        int (*open)(void*, unsigned channel, uint64_t generation, const std::atomic_bool& cancel) = nullptr;
    };
    explicit OneSegPipeSink(Factory factory) : factory_(factory) {}
    ~OneSegPipeSink() override;
    bool open(unsigned channel, uint64_t generation, const std::atomic_bool& cancel) override;
    bool write(const uint8_t*, size_t, uint64_t first_pair, uint64_t generation,
               const std::atomic_bool& cancel) override;
    bool finish() override;
    const char* lastError() const override { return error_; }

  private:
    bool fail(const char* message);
    Factory factory_;
    int fd_ = -1;
    uint64_t generation_ = 0, next_pair_ = 0;
    char error_[128]{};
};
} // namespace platform::linux_runtime
