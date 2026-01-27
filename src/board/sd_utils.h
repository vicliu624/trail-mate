#pragma once

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

namespace sdutil
{

inline void setCsHigh(int pin)
{
    if (pin < 0)
    {
        return;
    }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
}

template <typename Lockable>
inline bool installSpiSd(Lockable& bus, int sd_cs, uint32_t spi_hz, const char* mount_point,
                         const int* extra_cs, size_t extra_cs_count,
                         uint8_t* out_card_type = nullptr, uint32_t* out_card_size_mb = nullptr)
{
    if (sd_cs < 0)
    {
        return false;
    }

    // De-conflict shared SPI devices by driving their CS lines high.
    for (size_t i = 0; i < extra_cs_count; ++i)
    {
        setCsHigh(extra_cs[i]);
    }
    setCsHigh(sd_cs);

    // Ensure SPI bus pins are initialized for SD access.
    pinMode(MISO, INPUT_PULLUP);
    SPI.begin(SCK, MISO, MOSI);

    bool ok = false;
    uint8_t card_type = CARD_NONE;
    uint32_t card_size_mb = 0;

    if (bus.lock(pdMS_TO_TICKS(300)))
    {
        ok = SD.begin(sd_cs, SPI, spi_hz, mount_point);
        if (ok)
        {
            card_type = SD.cardType();
            if (card_type != CARD_NONE)
            {
                card_size_mb = static_cast<uint32_t>(SD.cardSize() / (1024 * 1024));
            }
            else
            {
                ok = false;
                SD.end();
            }
        }
        bus.unlock();
    }

    if (out_card_type)
    {
        *out_card_type = card_type;
    }
    if (out_card_size_mb)
    {
        *out_card_size_mb = card_size_mb;
    }
    return ok;
}

} // namespace sdutil
