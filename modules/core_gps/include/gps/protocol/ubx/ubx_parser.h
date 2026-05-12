#pragma once

#include <cstddef>
#include <cstdint>

namespace gps::ubx
{

class UbxParser
{
  public:
    void reset() {}
    void feed(const uint8_t* bytes, std::size_t len)
    {
        (void)bytes;
        (void)len;
    }
};

} // namespace gps::ubx
