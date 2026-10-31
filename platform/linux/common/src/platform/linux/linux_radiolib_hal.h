#pragma once

#include "platform/linux/packet_radio.h"
#include <Hal.h>
#include <chrono>
#include <map>
#include <string>

namespace platform::linux_runtime
{
class LinuxRadioLibHal final : public RadioLibHal
{
  public:
    static constexpr uint32_t kKernelChipSelect = 0xfffffffeU;
    explicit LinuxRadioLibHal(PacketRadioHardware hardware);
    ~LinuxRadioLibHal() override;
    void init() override;
    void term() override;
    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void attachInterrupt(uint32_t, void (*)(void), uint32_t) override;
    void detachInterrupt(uint32_t) override;
    void delay(RadioLibTime_t milliseconds) override;
    void delayMicroseconds(RadioLibTime_t microseconds) override;
    RadioLibTime_t millis() override;
    RadioLibTime_t micros() override;
    long pulseIn(uint32_t, uint32_t, RadioLibTime_t) override;
    void spiBegin() override;
    void spiBeginTransaction() override;
    void spiTransfer(uint8_t* output, size_t size, uint8_t* input) override;
    void spiEndTransaction() override;
    void spiEnd() override;
    void yield() override;
    bool healthy() const { return error_.empty(); }
    // Caller holds the driver mutex/exclusive coordinator lease. Switch the
    // kernel device limit and each transfer together; restore before reset/PHY.
    bool setIqTransferMode(bool enabled);
    uint32_t spiClockHz() const { return iq_mode_ ? 16000000u : hardware_.bus.spi_speed_hz; }
    const std::string& error() const { return error_; }

  private:
    void fail(const char* operation);
    bool waitReady();
    PacketRadioHardware hardware_;
    int spi_ = -1;
    bool iq_mode_ = false;
    int chip_ = -1;
    std::map<uint32_t, int> pins_;
    std::string error_;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point busy_since_{};
};
} // namespace platform::linux_runtime
