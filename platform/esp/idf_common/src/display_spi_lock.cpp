#include "display/DisplayInterface.h"

bool display_spi_lock(TickType_t xTicksToWait)
{
    (void)xTicksToWait;
    return true;
}

void display_spi_unlock() {}