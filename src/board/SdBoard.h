#pragma once

// SD card capability interface (kept separate from BoardBase).
class SdBoard
{
  public:
    virtual ~SdBoard() = default;
    virtual bool installSD() = 0;
    virtual void uninstallSD() = 0;
    virtual bool isCardReady() = 0;
};
