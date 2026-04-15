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
    // T-Deck colors skew purple/blue when RGB565 bytes are forced MSB-first on the
    // wire. Keep byte order local to this driver so color truth stays centralized.
    static constexpr DispTransferConfig_t getTransferConfig() { return DispTransferConfig_t{false}; }
    static constexpr size_t getRotationConfigCount() { return 4; }
};

} // namespace drivers
} // namespace display
