#pragma once

#include <cstddef>
#include <cstdint>

namespace boards::tab5
{

class CodecCompat
{
  public:
    CodecCompat() = default;
    ~CodecCompat();

    int open(uint8_t bits_per_sample, uint8_t channel, uint32_t sample_rate);
    void close();

    int write(uint8_t* buffer, size_t size);
    int read(uint8_t* buffer, size_t size);

    void setMute(bool enable);
    bool getMute() const;

    void setOutMute(bool enable);
    bool getOutMute() const;

    void setVolume(uint8_t level);
    int getVolume() const;

    void setGain(float db_value);
    float getGain() const;

    bool ready() const;

  private:
    bool ensure_ready();
    bool ensure_scratch(size_t size);

    void* scratch_ = nullptr;
    size_t scratch_size_ = 0;
    bool ready_ = false;
    bool mute_ = false;
    bool out_mute_ = false;
    int volume_ = 10;
    float gain_db_ = 0.0f;
    uint8_t requested_channels_ = 1;
    uint8_t requested_bits_ = 16;
    uint32_t requested_sample_rate_ = 8000;
    uint8_t hardware_capture_channels_ = 1;
};

} // namespace boards::tab5
