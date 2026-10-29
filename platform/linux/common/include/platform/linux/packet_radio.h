#pragma once

#include "platform/linux/fsk_settings.h"
#include "platform/linux/sx126x_radio.h"
#include <memory>

namespace platform::linux_runtime
{
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
