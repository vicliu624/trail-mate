#pragma once

namespace mesh
{

class IMeshRuntime
{
  public:
    virtual ~IMeshRuntime() = default;

    virtual void requestTick() = 0;
};

} // namespace mesh
