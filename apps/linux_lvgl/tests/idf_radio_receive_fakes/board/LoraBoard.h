#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

class LoraBoard
{
  public:
    bool online = true;
    uint32_t irq = 0;
    int length = 8, read_result = 0, start_result = 0;
    unsigned starts = 0, reads = 0, clears = 0;
    bool isRadioOnline() const { return online; }
    int startRadioReceive()
    {
        ++starts;
        return start_result;
    }
    uint32_t getRadioIrqFlags() const { return irq; }
    void clearRadioIrqFlags(uint32_t flags)
    {
        ++clears;
        irq &= ~flags;
    }
    int getRadioPacketLength(bool) const { return length; }
    int readRadioData(uint8_t* output, std::size_t size)
    {
        ++reads;
        // Model RadioLib's explicit-header validation. Clearing its metadata
        // before readData destroys the evidence needed to accept the packet.
        if (!(irq & 0x0002) || !(irq & 0x0010)) return -1;
        if (read_result) return read_result;
        std::memset(output, 0x42, size);
        return 0;
    }
    float getRadioRSSI() const { return -55; }
    float getRadioSNR() const { return 9; }
};
