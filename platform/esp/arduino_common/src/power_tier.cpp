#include "platform/esp/arduino_common/power_tier.h"

#include "board/BoardBase.h"

int getPowerTier()
{
    return board.getPowerTier();
}
