#pragma once
#include "ui/app_screen.h"
namespace ui::fm
{
// Target runtime decides availability. Unsupported targets get no descriptor.
#if (defined(ESP_PLATFORM) && defined(TRAIL_MATE_IDF_RADIO_LR2021)) || defined(TRAIL_MATE_PLATFORM_LINUX) || defined(TRAIL_MATE_EXTERNAL_FM_RUNTIME)
AppScreen* application();
#else
inline AppScreen* application()
{
    return nullptr;
}
#endif
} // namespace ui::fm
