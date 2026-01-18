#include "gps/gps_hw_status.h"
#include "board/TLoRaPagerBoard.h"

bool gps_hw_is_ready()
{
    return instance.isGPSReady();
}

bool sd_hw_is_ready()
{
    return instance.isSDReady();
}
