#pragma once

#include <cstddef>
#include <cstdint>

// LoRa ӿڣṩ RadioLib ķʡ
class LoraBoard
{
  public:
    virtual ~LoraBoard() = default;

    virtual bool isRadioOnline() const = 0;

    virtual int transmitRadio(const uint8_t* data, size_t len) = 0;
    virtual int startRadioReceive() = 0;
    virtual uint32_t getRadioIrqFlags() = 0;
    virtual int getRadioPacketLength(bool update) = 0;
    virtual int readRadioData(uint8_t* buf, size_t len) = 0;
    virtual void clearRadioIrqFlags(uint32_t flags) = 0;
    virtual float getRadioRSSI() = 0;
    virtual float getRadioSNR() = 0;

    // Board-specific LoRa configuration without exposing SX126x types.
    virtual void configureLoraRadio(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr_denom,
                                    int8_t tx_power, uint16_t preamble_len, uint8_t sync_word,
                                    uint8_t crc_len) = 0;
};
