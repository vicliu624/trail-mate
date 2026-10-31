#pragma once

#include "platform/linux/fsk_settings.h"
#include "platform/linux/lr2021_rf_switch.h"
#include "platform/linux/oneseg_iq_stream.h"
#include "platform/linux/sx126x_radio.h"
#include <memory>

namespace platform::shared::radio
{
struct FmIqRequest;
struct FmIqResult;
} // namespace platform::shared::radio

namespace platform::linux_runtime
{
using FmIqRequest = platform::shared::radio::FmIqRequest;
using FmIqResult = platform::shared::radio::FmIqResult;
// Preserve the existing Linux wire/config ABI during backend extraction.
// These are modulation and packet values; no SX126x command appears in this port.
using RadioSettings = Sx126xLoRaConfig;
using RadioPacket = Sx126xPacket;
using RadioStatistics = Sx126xRadioStats;

struct PacketRadioHardware
{
    std::string driver = "sx126x";
    Sx126xRadioConfig bus{};
    float tcxo_voltage = 0.0f;
    Lr2021RfSwitchProfile rf_switch_profile = Lr2021RfSwitchProfile::None;
};

// Matches the pinned portable RX engine's error domain without exposing that
// optional dependency through every packet-radio consumer's public headers.
enum class IqCaptureError : int
{
    Argument = -1,
    State = -2,
    Timeout = -3,
    Data = -4,
    Io = -5
};
struct RadioIqCaptureResult
{
    std::size_t bytes = 0;
    uint32_t nominal_rate_hz = 0; // Zero is unknown, never an assumed clock.
    uint64_t elapsed_us = 0;
    int capture_error = 0, restore_error = 0, recovery_error = 0;
};

class IPacketRadio
{
  public:
    virtual ~IPacketRadio() = default;
    virtual const char* name() const = 0;
    virtual bool hardwareCandidatePresent() const = 0;
    virtual bool acquire() = 0;
    virtual void release() = 0;
    // A plain driver cannot attest that a coordinator restored another user.
    // This default still releases ownership; wrappers may provide checked restore.
    virtual bool releaseWithStatus()
    {
        release();
        return false;
    }
    virtual bool configureLoRa(const RadioSettings& settings) = 0;
    virtual bool supportsFsk() const { return false; }
    virtual bool supportsFskConfiguration(const FskSettings&) const { return false; }
    virtual bool configureFsk(const FskSettings&) { return false; }
    virtual bool startReceive() = 0;
    virtual bool transmit(const std::uint8_t* data, std::size_t size) = 0;
    virtual bool pollReceive(RadioPacket* packet) = 0;
    virtual float readRssi() = 0;
    virtual bool isOnline() const = 0;
    virtual const char* lastError() const = 0;
    virtual RadioSettings appliedLoRaConfig() const = 0;
    virtual RadioStatistics stats() const = 0;
    // Capability queries perform no I/O. Capture is synchronous and requires
    // an acquired foreground coordinator lease; run it off the UI thread.
    // Both non-overlapping buffers hold >=256 words. Only a successful call
    // publishes bytes, after SRAM and prior modem/RX restoration are verified.
    virtual bool supportsIqCapture() const { return false; }
    virtual bool captureIq(uint32_t*, uint32_t*, std::size_t, RadioIqCaptureResult& result)
    {
        result = {};
        result.capture_error = static_cast<int>(IqCaptureError::State);
        return false;
    }
    // Continuous TV IQ; synchronous worker operation under an exclusive lease.
    // A backend capability is not evidence of RF lock, TS or media playback.
    virtual bool supportsOneSegIq() const { return false; }
    virtual bool streamOneSegIq(const OneSegIqRequest&, OneSegIqResult& result)
    {
        result = {};
        result.capture_error = static_cast<int>(IqCaptureError::State);
        return false;
    }
    // Experimental receive-only FM stream, worker-only under a foreground lease.
    // Capability is a backend path, not RF qualification or station reception.
    virtual bool supportsFmIq() const { return false; }
    virtual bool streamFmIq(const FmIqRequest&, FmIqResult&) { return false; }
};

// The backend factory never silently substitutes an SX126x for an LR2021.
std::unique_ptr<IPacketRadio> make_lr2021_radio(const PacketRadioHardware& hardware);
std::unique_ptr<IPacketRadio> make_unavailable_packet_radio(const char* reason);

// One physical radio, two clients. Foreground tools temporarily suspend mesh
// I/O; mesh configuration changes are deferred and restored on tool release.
// Software permission to use the radio. Disabling may request standby; it does
// not imply that a board without a supply-control pin can cut physical power.
class IRadioPowerControl
{
  public:
    virtual ~IRadioPowerControl() = default;
    virtual bool setEnabled(bool enabled) = 0;
    virtual bool enabled() const = 0;
};
class PacketRadioCoordinator : public IRadioPowerControl
{
  public:
    explicit PacketRadioCoordinator(std::unique_ptr<IPacketRadio> driver);
    ~PacketRadioCoordinator() override;
    IPacketRadio& mesh();
    IPacketRadio& foreground();
    bool setEnabled(bool enabled) override;
    bool enabled() const override;
    PacketRadioCoordinator(const PacketRadioCoordinator&) = delete;
    PacketRadioCoordinator& operator=(const PacketRadioCoordinator&) = delete;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Configure once in the owning composition, before obtaining any client.
bool configure_packet_radio(const PacketRadioHardware& hardware);
IPacketRadio& mesh_packet_radio();
IPacketRadio& foreground_packet_radio();
bool set_packet_radio_enabled(bool enabled);
bool packet_radio_enabled();
IRadioPowerControl& packet_radio_power();
} // namespace platform::linux_runtime
