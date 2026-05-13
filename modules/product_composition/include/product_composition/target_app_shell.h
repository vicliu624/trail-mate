#pragma once

namespace product_composition
{

class ITargetAppShell
{
  public:
    virtual ~ITargetAppShell() = default;

    virtual bool initialize() = 0;
    virtual void tick() = 0;
    virtual void shutdown() = 0;
};

} // namespace product_composition
