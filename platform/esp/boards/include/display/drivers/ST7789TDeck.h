#pragma once

#include "display/DisplayInterface.h"
#include <stddef.h>
#include <stdint.h>

namespace display
{
namespace drivers
{

// ST7789 init/rotation config tuned from LilyGo T-Deck TFT_eSPI setup.
class ST7789TDeck
{
  public:
    static const CommandTable_t* getInitCommands();
    static size_t getInitCommandsCount();
    static const DispRotationConfig_t* getRotationConfig(uint16_t width, uint16_t height);
    // Historical T-Deck path used lv_draw_sw_rgb565_swap() before the SPI flush,
    // which means this panel wants RGB565 on the wire in MSB-first order.
    // Keep that truth here in the driver so LVGL stays generic.
    static constexpr DispTransferConfig_t getTransferConfig() { return DispTransferConfig_t{true}; }
    static constexpr size_t getRotationConfigCount() { return 4; }
};

} // namespace drivers
} // namespace display
