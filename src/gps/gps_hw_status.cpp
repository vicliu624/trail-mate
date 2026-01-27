#include "gps/gps_hw_status.h"
#include "board/BoardBase.h"

bool gps_hw_is_ready()
{
    return board.isGPSReady();
}

bool sd_hw_is_ready()
{
    return board.isSDReady();
}
