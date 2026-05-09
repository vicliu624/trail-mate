#pragma once

#include "TLoRaPagerTypes.h"

class GPS;

// Shared GPS board capability contract.
class GpsBoard
{
  public:
    virtual ~GpsBoard() = default;

    virtual bool initGPS() = 0;
    virtual void setGPSOnline(bool online) = 0;
    virtual void deinitGPS() { setGPSOnline(false); }
    virtual GPS& getGPS() = 0;
    virtual bool isGPSReady() const = 0;
    virtual void powerControl(PowerCtrlChannel_t ch, bool enable) = 0;
    virtual bool syncTimeFromGPS(uint32_t gps_task_interval_ms = 0) = 0;
};
