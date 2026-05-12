#pragma once

#include "gps/domain/location_fix.h"

namespace gps
{

class ILocationSource
{
  public:
    virtual ~ILocationSource() = default;

    virtual bool latestFix(LocationFix& out) const = 0;
};

class NullLocationSource final : public ILocationSource
{
  public:
    bool latestFix(LocationFix& out) const override
    {
        out = {};
        return false;
    }
};

inline const ILocationSource& nullLocationSource()
{
    static NullLocationSource source;
    return source;
}

} // namespace gps
