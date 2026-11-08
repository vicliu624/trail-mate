#pragma once

#include <cstdint>
#include <string>

namespace platform::linux_runtime
{

struct CardputerCapObservation
{
    bool radio_enabled = true;
    bool radio_online = false;
    bool gps_enabled = true;
    uint32_t gps_chars = 0;
    std::string radio_error;
    bool radio_endpoint_present = true;
};

// The Cap shares its supply between LoRa and GPS. Own the supply outside the
// application session so all radio/UART workers stop before the lease restores it.
class CardputerCap
{
  public:
    explicit CardputerCap(std::string power_path = "/sys/class/leds/ext_5v_out/brightness",
                          uint32_t settle_ms = 250);
    ~CardputerCap();
    CardputerCap(const CardputerCap&) = delete;
    CardputerCap& operator=(const CardputerCap&) = delete;

    void begin();
    bool pending() const { return active_ && !decided_; }
    // Called on the UI thread after startup. The returned notice is emitted at
    // most once; an enabled UART without received bytes is not presence evidence.
    const char* takeNotice(uint64_t elapsed_ms, const CardputerCapObservation& observation);

  private:
    std::string power_path_;
    uint32_t settle_ms_;
    int original_power_ = 0;
    bool active_ = false;
    bool restore_power_ = false;
    bool power_failed_ = false;
    bool decided_ = false;
};

} // namespace platform::linux_runtime
