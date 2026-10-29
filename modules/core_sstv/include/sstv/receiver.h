#pragma once
#include "sstv/decode_sstv.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace sstv::core
{
enum class ReceiveState
{
    Waiting,
    Receiving,
    Complete
};
struct ReceiveStatus
{
    ReceiveState state = ReceiveState::Waiting;
    uint16_t line = 0;
    float progress = 0, audio_level = 0;
    bool has_image = false;
};
// Own this image-bearing object in long-lived/heap storage, never on ESP task
// stacks. One owner feeds PCM and reads its frame; UI publication is an adapter
// responsibility. No clock, device, filesystem or UI dependency lives here.
class Receiver
{
  public:
    static constexpr unsigned sample_rate = 15000, width = 288, height = 192;
    Receiver();
    void reset();
    void feed(const int16_t* samples, std::size_t count);
    ReceiveStatus status() const { return status_; }
    const uint16_t* frame() const { return frame_.data(); }
    const char* mode_name() const;

  private:
    void pixel(uint16_t y, uint16_t x, uint8_t colour, uint8_t value);
    void render_line(uint16_t y);
    void render_row(uint16_t y, uint16_t native_height, unsigned channel, bool yuv, unsigned cr = 1, unsigned cb = 2);
    c_sstv_decoder decoder_;
    std::array<uint16_t, width * height> frame_{};
    std::array<std::array<uint8_t, 4>, 320> line_{};
    ReceiveStatus status_;
    e_mode mode_ = martin_m1;
    uint16_t last_y_ = 0;
    bool have_line_ = false;
};
} // namespace sstv::core
