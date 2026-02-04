#include "hal/hal_gps.h"

#include "../gps/GPS.h"
#include "board/GpsBoard.h"
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

bool HalGps::hasAltitude() const
{
    return board_ != nullptr && board_->getGPS().altitude.isValid();
}

double HalGps::altitude() const
{
    return board_ != nullptr ? board_->getGPS().altitude.meters() : 0.0;
}

bool HalGps::hasSpeed() const
{
    return board_ != nullptr && board_->getGPS().speed.isValid();
}

double HalGps::speed() const
{
    return board_ != nullptr ? board_->getGPS().speed.mps() : 0.0;
}

bool HalGps::hasCourse() const
{
    return board_ != nullptr && board_->getGPS().course.isValid();
}

double HalGps::course() const
{
    return board_ != nullptr ? board_->getGPS().course.deg() : 0.0;
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

bool HalGps::applyGnssConfig(uint8_t mode, uint8_t sat_mask)
{
    if (board_ == nullptr)
    {
        return false;
    }

    GPS& gps = board_->getGPS();
    bool ok_mode = gps.setReceiverMode(mode, sat_mask);
    bool ok_gnss = gps.configureGnss(sat_mask);
    return ok_mode && ok_gnss;
}

bool HalGps::applyNmeaConfig(uint8_t output_hz, uint8_t sentence_mask)
{
    if (board_ == nullptr)
    {
        return false;
    }

    return board_->getGPS().configureNmeaOutput(output_hz, sentence_mask);
}

} // namespace hal
