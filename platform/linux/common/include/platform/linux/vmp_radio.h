#pragma once
#include "platform/linux/packet_radio.h"

namespace platform::linux_runtime
{
// VMP v1 PHY profile 1. Channel selection is a protocol value, not board wiring.
inline bool vmp_media_settings(uint8_t channel, FskSettings& out)
{
    out = {};
    if (channel > 39) return false;
    out.freq_mhz = 2402.0f + channel * 2.0f;
    out.bitrate_kbps = 500;
    out.deviation_khz = 250;
    out.rx_bandwidth_khz = 800;
    out.tx_power_dbm = 10;
    out.preamble_bits = 16;
    out.sync_word = {'V', 'M', 'P', 1};
    out.sync_word_size = 4;
    return true;
}
// Worker-owned raw RF lease, separate from every mesh codec and TX queue.
class VmpRadioLease
{
  public:
    explicit VmpRadioLease(IPacketRadio& foreground) : radio_(foreground) {}
    ~VmpRadioLease() { end(); }
    VmpRadioLease(const VmpRadioLease&) = delete;
    VmpRadioLease& operator=(const VmpRadioLease&) = delete;
    bool supports(uint8_t channel) const;
    bool begin(uint8_t channel);
    bool switchToMedia();
    bool transmit(const uint8_t*, size_t);
    bool receive(RadioPacket&);
    bool end();
    bool active() const { return acquired_; }
    bool mediaActive() const { return acquired_ && media_; }
    const char* lastError() const { return error_.c_str(); }

  private:
    bool fail(const char* error);
    IPacketRadio& radio_;
    RadioSettings control_;
    FskSettings profile_;
    bool acquired_ = false, media_ = false, last_restore_ok_ = true;
    std::string error_;
};
} // namespace platform::linux_runtime
