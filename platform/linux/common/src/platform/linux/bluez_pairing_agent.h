#pragma once
#if defined(TRAIL_MATE_BLUEZ_GIO)
#include "platform/linux/bluez_phone_gatt.h"
#include <chrono>
#include <gio/gio.h>

namespace platform::linux_runtime::detail
{
// Borrows a dedicated connection/context owned by the GATT worker.
class BluezPairingAgent
{
  public:
    BluezPairingAgent(GDBusConnection*, GMainContext*, std::string owner,
                      std::string adapter, std::string service,
                      BluezPhoneGattOptions, std::function<bool(const PhonePairingDisplay&)>,
                      std::function<bool(const std::string&)> allowed);
    ~BluezPairingAgent();
    bool start();
    void stop();
    void clear(const std::string& peer = {});
    void poll();
    bool ready() const { return active_; }
    const std::string& error() const { return error_; }

  private:
    bool rpc(const char* method, GVariant* parameters);
    bool display(const std::string& peer, uint32_t pin, bool fixed);
    static void method(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant*, GDBusMethodInvocation*, gpointer);
    void dispatch(const char*, const char*, GVariant*, GDBusMethodInvocation*);
    GDBusConnection* connection_;
    GMainContext* context_;
    std::string owner_, adapter_, service_;
    BluezPhoneGattOptions options_;
    std::function<bool(const PhonePairingDisplay&)> presenter_;
    std::function<bool(const std::string&)> allowed_;
    guint registration_ = 0;
    GDBusNodeInfo* description_ = nullptr;
    bool registered_ = false, active_ = false;
    std::string peer_, error_;
    uint32_t pin_ = 0;
    std::chrono::steady_clock::time_point expires_;
};
} // namespace platform::linux_runtime::detail
#endif
