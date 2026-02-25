#include "app/power_tier.h"
#include "board/BoardBase.h"

extern BoardBase& board;

int getPowerTier()
{
    return board.getPowerTier();
}
