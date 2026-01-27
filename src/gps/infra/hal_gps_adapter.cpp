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

uint8_t HalGpsAdapter::satellites() const
{
    return hal_gps_.satellites();
}

bool HalGpsAdapter::syncTime(uint32_t gps_task_interval_ms)
{
    return hal_gps_.syncTime(gps_task_interval_ms);
}

} // namespace gps
