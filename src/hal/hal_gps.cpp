#include "hal/hal_gps.h"

#include "board/GpsBoard.h"
#include "../gps/GPS.h"
#include "board/TLoRaPagerTypes.h"
#include "pins_arduino.h"

namespace hal
{

void HalGps::begin(GpsBoard& board)
{
    board_ = &board;
}

bool HalGps::isReady() const
{
    return board_ != nullptr && board_->isGPSReady();
}

bool HalGps::init()
{
    if (board_ == nullptr)
    {
        return false;
    }
    return board_->initGPS();
}

void HalGps::powerOn()
{
    if (board_ == nullptr)
    {
        return;
    }
    board_->powerControl(POWER_GPS, true);
    delay(10);
}

void HalGps::powerOff()
{
    if (board_ == nullptr)
    {
        return;
    }
    Serial1.end();
    board_->powerControl(POWER_GPS, false);
    board_->setGPSOnline(false);
}

uint32_t HalGps::loop()
{
    if (board_ == nullptr)
    {
        return 0;
    }
    return board_->getGPS().loop();
}

bool HalGps::hasFix() const
{
    return board_ != nullptr && board_->getGPS().location.isValid();
}

double HalGps::latitude() const
{
    return board_ != nullptr ? board_->getGPS().location.lat() : 0.0;
}

double HalGps::longitude() const
{
    return board_ != nullptr ? board_->getGPS().location.lng() : 0.0;
}

uint8_t HalGps::satellites() const
{
    return board_ != nullptr ? board_->getGPS().satellites.value() : 0;
}

bool HalGps::syncTime(uint32_t gps_task_interval_ms)
{
    if (board_ == nullptr)
    {
        return false;
    }
    return board_->syncTimeFromGPS(gps_task_interval_ms);
}

} // namespace hal
