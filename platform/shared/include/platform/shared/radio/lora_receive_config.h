#pragma once

namespace platform::shared::radio
{
// Own this cache with the radio, under the same lock. Config is a small value
// type; the caller validates requests before touching hardware. Radio exposes
// RadioLib's LoRa setters, and check accepts (result, operation).
template <typename Config>
class LoRaReceiveConfig
{
  public:
    void invalidate() { valid_ = false; }

    template <typename Radio, typename Check>
    bool apply(Radio& radio, const Config& next, Check check)
    {
        const bool full = !valid_;
        const bool frequency_changed = full || next.freq_mhz != applied_.freq_mhz;
        valid_ = false;
        if (!check(radio.standby(), "standby")) return false;
        if (frequency_changed && !check(radio.setFrequency(next.freq_mhz), "frequency")) return false;
        if ((full || next.bw_khz != applied_.bw_khz) && !check(radio.setBandwidth(next.bw_khz), "bandwidth")) return false;
        if ((full || next.sf != applied_.sf) && !check(radio.setSpreadingFactor(next.sf), "spreading factor")) return false;
        if ((full || next.cr != applied_.cr) && !check(radio.setCodingRate(next.cr), "coding rate")) return false;
        if ((full || next.sync_word != applied_.sync_word) && !check(radio.setSyncWord(next.sync_word), "sync word")) return false;
        // RadioLib selects the LR2021 LF/HF PA table from the last frequency.
        // Retunes must reapply power even when the requested dBm is unchanged.
        if ((frequency_changed || next.tx_power_dbm != applied_.tx_power_dbm) && !check(radio.setOutputPower(next.tx_power_dbm), "TX power")) return false;
        if ((full || next.preamble_len != applied_.preamble_len) && !check(radio.setPreambleLength(next.preamble_len), "preamble")) return false;
        if ((full || next.crc_len != applied_.crc_len) && !check(radio.setCRC(next.crc_len), "CRC")) return false;
        if (full && (!check(radio.explicitHeader(), "explicit header") || !check(radio.invertIQ(false), "IQ polarity"))) return false;
        // Even an identical candidate needs a clean receive/IRQ generation.
        if (!check(radio.startReceive(), "start receive")) return false;
        applied_ = next;
        valid_ = true;
        return true;
    }

  private:
    Config applied_{};
    bool valid_ = false;
};
} // namespace platform::shared::radio
