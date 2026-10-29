#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace platform::linux_runtime
{
// Single-owner capture port. SDL's callback writes into a fixed one-second
// ring; slow consumers lose the oldest samples rather than growing memory.
class SdlPcmInput
{
  public:
    SdlPcmInput();
    ~SdlPcmInput();
    SdlPcmInput(const SdlPcmInput&) = delete;
    SdlPcmInput& operator=(const SdlPcmInput&) = delete;
    bool available() const;
    bool begin(uint32_t rate, const char* device_name = nullptr);
    std::size_t read(int16_t* output, std::size_t capacity);
    std::size_t buffered_samples() const;
    uint64_t dropped_samples() const;
    bool active() const;
    void end();
    // Worker-only intentional discard; does not count as callback overrun.
    void discard_pending();
    const char* last_error() const { return error_.c_str(); }

  private:
    struct State;
    static void capture(void* context, uint8_t* stream, int bytes);
    bool initialized_ = false;
    uint32_t device_ = 0;
    std::unique_ptr<State> state_;
    std::string error_;
};
} // namespace platform::linux_runtime
