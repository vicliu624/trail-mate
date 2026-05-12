#pragma once

#include <stdint.h>

namespace mesh
{

class IClock
{
  public:
    virtual ~IClock() = default;

    virtual uint32_t nowMs() const = 0;
};

} // namespace mesh
