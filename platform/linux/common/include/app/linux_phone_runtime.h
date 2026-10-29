#pragma once
#include "platform/linux/bluez_phone_gatt.h"
#include <memory>

namespace trailmate::linux_app
{
class LinuxAppServices;
class LinuxPhoneRuntime
{
  public:
    struct Status
    {
        bool ready = false;
        bool connected = false;
        std::string error;
    };
    explicit LinuxPhoneRuntime(LinuxAppServices& services, std::function<bool(const platform::linux_runtime::PhonePairingDisplay&)> presenter = {});
    void setPairingPresenter(std::function<bool(const platform::linux_runtime::PhonePairingDisplay&)>);
    ~LinuxPhoneRuntime();
    LinuxPhoneRuntime(const LinuxPhoneRuntime&) = delete;
    LinuxPhoneRuntime& operator=(const LinuxPhoneRuntime&) = delete;
    // Application thread only. Registration/discovery are asynchronous to the UI.
    void configure(bool enabled, const platform::linux_runtime::BluezPhoneGattOptions& options);
    void tick();
    void stop();
    Status status() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace trailmate::linux_app
