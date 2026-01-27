#include "board/TDeckBoard.h"

TDeckBoard::TDeckBoard() : LilyGo_Display(SPI_DRIVER, false) {}

TDeckBoard* TDeckBoard::getInstance()
{
    static TDeckBoard instance;
    return &instance;
}

uint32_t TDeckBoard::begin(uint32_t disable_hw_init)
{
    (void)disable_hw_init;
    // Minimal probe flags so higher layers can run without board-specific init.
    devices_probe_ = HW_RADIO_ONLINE;
    return devices_probe_;
}

namespace
{
TDeckBoard& getInstanceRef()
{
    return *TDeckBoard::getInstance();
}
} // namespace

TDeckBoard& instance = getInstanceRef();
BoardBase& board = getInstanceRef();
