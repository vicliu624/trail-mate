#include "display/DisplayConfig.h"

namespace display
{

Config get_config()
{
    Config config{};

#if defined(DISPLAY_DRIVER_ST7796)
    config.driver = Driver::ST7796;
#elif defined(DISPLAY_DRIVER_ST7789V2)
    config.driver = Driver::ST7789V2;
#else
    config.driver = Driver::Unknown;
#endif

#if defined(SCREEN_WIDTH) && defined(SCREEN_HEIGHT)
    config.screen = {SCREEN_WIDTH, SCREEN_HEIGHT};
#else
    config.screen = {0, 0};
#endif

    return config;
}

} // namespace display
