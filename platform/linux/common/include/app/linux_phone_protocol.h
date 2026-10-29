#pragma once
#include "platform/linux/bluez_phone_gatt.h"
#include <array>
#include <memory>

namespace trailmate::linux_app
{
class LinuxAppServices;
// Shared phone sessions run on the application thread. Transport requests must be
// marshalled here by the worker; no application configuration crosses threads.
class LinuxPhoneProtocol
{
  public:
    struct Notification
    {
        uint64_t token = 0;
        platform::linux_runtime::PhoneGattChannel channel{};
        std::vector<uint8_t> bytes;
    };
    explicit LinuxPhoneProtocol(LinuxAppServices& services);
    ~LinuxPhoneProtocol();
    LinuxPhoneProtocol(const LinuxPhoneProtocol&) = delete;
    LinuxPhoneProtocol& operator=(const LinuxPhoneProtocol&) = delete;
    bool supports(platform::linux_runtime::PhoneGattProtocol) const;
    bool pairingOptions(platform::linux_runtime::BluezPhoneGattOptions&) const;
    const std::string& pairingError() const;
    void setPairingDisplay(const platform::linux_runtime::PhonePairingDisplay&);
    bool connect(platform::linux_runtime::PhoneGattProtocol, const std::array<uint8_t, 6>& device_mac);
    void disconnect();
    void subscribe(platform::linux_runtime::PhoneGattChannel, bool enabled);
    bool read(platform::linux_runtime::PhoneGattChannel, std::vector<uint8_t>&);
    bool write(platform::linux_runtime::PhoneGattChannel, const std::vector<uint8_t>&);
    void pump();
    bool nextNotification(Notification& out);
    void commitNotification(uint64_t token);
    bool takeDisconnectRequest();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace trailmate::linux_app
