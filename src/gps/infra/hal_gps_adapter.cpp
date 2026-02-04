#include "gps/infra/hal_gps_adapter.h"
#include "board/GpsBoard.h"

namespace gps
{

void HalGpsAdapter::begin(GpsBoard& board)
{
    hal_gps_.begin(board);
}

bool HalGpsAdapter::isReady() const
{
    return hal_gps_.isReady();
}

bool HalGpsAdapter::init()
{
    return hal_gps_.init();
}

void HalGpsAdapter::powerOn()
{
    hal_gps_.powerOn();
}

void HalGpsAdapter::powerOff()
{
    hal_gps_.powerOff();
}

uint32_t HalGpsAdapter::loop()
{
    return hal_gps_.loop();
}

bool HalGpsAdapter::hasFix() const
{
    return hal_gps_.hasFix();
}

double HalGpsAdapter::latitude() const
{
    return hal_gps_.latitude();
}

double HalGpsAdapter::longitude() const
{
    return hal_gps_.longitude();
}

bool HalGpsAdapter::hasAltitude() const
{
    return hal_gps_.hasAltitude();
}

double HalGpsAdapter::altitude() const
{
    return hal_gps_.altitude();
}

bool HalGpsAdapter::hasSpeed() const
{
    return hal_gps_.hasSpeed();
}

double HalGpsAdapter::speed() const
{
    return hal_gps_.speed();
}

bool HalGpsAdapter::hasCourse() const
{
    return hal_gps_.hasCourse();
}

double HalGpsAdapter::course() const
{
    return hal_gps_.course();
}

uint8_t HalGpsAdapter::satellites() const
{
    return hal_gps_.satellites();
}

bool HalGpsAdapter::syncTime(uint32_t gps_task_interval_ms)
{
    return hal_gps_.syncTime(gps_task_interval_ms);
}

bool HalGpsAdapter::applyGnssConfig(uint8_t mode, uint8_t sat_mask)
{
    return hal_gps_.applyGnssConfig(mode, sat_mask);
}

bool HalGpsAdapter::applyNmeaConfig(uint8_t output_hz, uint8_t sentence_mask)
{
    return hal_gps_.applyNmeaConfig(output_hz, sentence_mask);
}

} // namespace gps
