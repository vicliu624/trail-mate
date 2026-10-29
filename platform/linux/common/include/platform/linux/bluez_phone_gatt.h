#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace platform::linux_runtime
{
enum class PhoneGattProtocol
{
    Meshtastic,
    MeshCore
};
enum class PhoneGattChannel
{
    ToRadio,
    FromRadio,
    FromNum,
    LogRadio,
    FromRadioSync,
    MeshCoreRx,
    MeshCoreTx
};
enum class PhonePairingMode
{
    SystemAgent,
    NoPin,
    RandomPin,
    FixedPin
};
struct PhonePairingDisplay
{
    bool active = false;
    uint32_t passkey = 0;
    bool fixed = false;
    std::string local_name;
};

struct BluezPhoneGattOptions
{
    PhoneGattProtocol protocol = PhoneGattProtocol::Meshtastic;
    std::string adapter_address;
    std::string local_name = "Trail Mate";
    bool encrypted = true;
    bool enable_from_radio_sync = false;
    PhonePairingMode pairing_mode = PhonePairingMode::SystemAgent;
    uint32_t fixed_pin = 0;
    // Empty uses the system bus. Explicit addresses support isolated integration tests.
    std::string bus_address;
};

struct BluezPhoneGattHooks
{
    using Bytes = std::vector<uint8_t>;
    std::function<bool(PhoneGattChannel, Bytes&)> read;
    std::function<bool(PhoneGattChannel, const Bytes&)> write;
    std::function<void(const std::string&)> connected;
    std::function<void()> disconnected;
    std::function<void(PhoneGattChannel, bool)> subscription;
    std::function<bool(const PhonePairingDisplay&)> pairing;
};

// All methods and hooks run on one owning worker thread, never the UI thread.
// This exports transport only; the owner binds the shared phone protocol sessions.
class BluezPhoneGatt
{
  public:
    BluezPhoneGatt();
    ~BluezPhoneGatt();
    BluezPhoneGatt(const BluezPhoneGatt&) = delete;
    BluezPhoneGatt& operator=(const BluezPhoneGatt&) = delete;

    bool start(const BluezPhoneGattOptions& options, BluezPhoneGattHooks hooks);
    void stop();
    void poll();
    bool ready() const;
    const std::string& error() const;
    // False leaves ownership with the protocol queue. Never truncates a frame.
    bool notify(PhoneGattChannel channel, const std::vector<uint8_t>& bytes);
    bool disconnectPhone();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
