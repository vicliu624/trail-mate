#pragma once

#include "gps/domain/location_fix.h"

namespace gps
{

class ILocationEventSink
{
  public:
    virtual ~ILocationEventSink() = default;

    virtual void onLocationUpdated(const LocationFix& fix) = 0;
};

class NullLocationEventSink final : public ILocationEventSink
{
  public:
    void onLocationUpdated(const LocationFix& fix) override
    {
        (void)fix;
    }
};

inline ILocationEventSink& nullLocationEventSink()
{
    static NullLocationEventSink sink;
    return sink;
}

} // namespace gps
