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
                         uint8_t* out_card_type = nullptr, uint32_t* out_card_size_mb = nullptr,
                         bool use_lock = true)
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
    // Use the same SPI host as the rest of the board to avoid dual-host pin conflicts.
    SPI.end();
    delay(2);
    SPI.begin(SCK, MISO, MOSI);
    SPIClass& sd_bus = SPI;
    // Re-assert CS lines after SPI re-init.
    for (size_t i = 0; i < extra_cs_count; ++i)
    {
        setCsHigh(extra_cs[i]);
    }
    setCsHigh(sd_cs);
    delay(2);
    Serial.printf("[SD] SPI pins sck=%d miso=%d mosi=%d cs=%d hz=%lu\n",
                  SCK, MISO, MOSI, sd_cs, (unsigned long)spi_hz);
    for (size_t i = 0; i < extra_cs_count; ++i)
    {
        Serial.printf("[SD] extra CS pin=%d level=%d\n", extra_cs[i], digitalRead(extra_cs[i]));
    }
    Serial.printf("[SD] sd CS pin=%d level=%d\n", sd_cs, digitalRead(sd_cs));

    bool ok = false;
    uint8_t card_type = CARD_NONE;
    uint32_t card_size_mb = 0;

    // Skip SPI locking to align with pager behavior during early SD init.
    (void)bus;
    (void)use_lock;
    bool locked = true;

    if (locked)
    {
        // Try a small frequency fallback ladder; some SD cards/rails are picky at boot.
        const uint32_t freqs[] = {spi_hz, 400000U, 200000U};
        for (size_t i = 0; i < (sizeof(freqs) / sizeof(freqs[0])); ++i)
        {
            const uint32_t hz_try = freqs[i];
            Serial.printf("[SD] try hz=%lu\n", (unsigned long)hz_try);
            ok = SD.begin(sd_cs, sd_bus, hz_try, mount_point);
            Serial.printf("[SD] SD.begin -> %d\n", ok ? 1 : 0);
            if (!ok)
            {
                // Some cores/boards are picky about the mount point overload.
                ok = SD.begin(sd_cs, sd_bus, hz_try);
                Serial.printf("[SD] SD.begin (no mount) -> %d\n", ok ? 1 : 0);
            }
            if (ok)
            {
                break;
            }
            SD.end();
            delay(5);
        }
        if (ok)
        {
            card_type = SD.cardType();
            Serial.printf("[SD] cardType=%u\n", (unsigned)card_type);
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
