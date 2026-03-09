#pragma once

#include "hostlink_types.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hostlink
{

struct Frame
{
    uint8_t type = 0;
    uint16_t seq = 0;
    std::vector<uint8_t> payload;
};

uint16_t crc16_ccitt(const uint8_t* data, size_t len);
bool encode_frame(uint8_t type, uint16_t seq, const uint8_t* payload, size_t len,
                  std::vector<uint8_t>& out);

class Decoder
{
  public:
    explicit Decoder(size_t max_len = kMaxFrameLen);
    void reset();
    void push(const uint8_t* data, size_t len);
    bool next(Frame& out);

  private:
    std::vector<uint8_t> buffer_;
    size_t max_len_;
};

} // namespace hostlink
