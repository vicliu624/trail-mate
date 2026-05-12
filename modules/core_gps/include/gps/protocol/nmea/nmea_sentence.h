#pragma once

#include <cstddef>
#include <cstdint>

namespace gps::nmea
{

constexpr std::size_t kMaxNmeaSentenceLen = 128;

struct NmeaSentence
{
    char text[kMaxNmeaSentenceLen] = {};
    std::size_t len = 0;
    bool has_checksum = false;
    bool checksum_valid = false;
};

class NmeaSentenceAssembler
{
  public:
    void reset();
    bool feed(uint8_t byte, NmeaSentence& out);

  private:
    char buffer_[kMaxNmeaSentenceLen] = {};
    std::size_t len_ = 0;
    bool collecting_ = false;
};

bool verifyChecksum(const char* sentence, bool* out_has_checksum = nullptr);

} // namespace gps::nmea
