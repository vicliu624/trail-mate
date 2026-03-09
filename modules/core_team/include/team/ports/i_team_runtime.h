#pragma once

#include <cstddef>
#include <cstdint>

namespace team
{

class ITeamRuntime
{
  public:
    virtual ~ITeamRuntime() = default;
    virtual uint32_t nowMillis() = 0;
    virtual uint32_t nowUnixSeconds() = 0;
    virtual void fillRandomBytes(uint8_t* out, size_t len) = 0;
};

} // namespace team
