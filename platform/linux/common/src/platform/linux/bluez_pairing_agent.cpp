#include "bluez_pairing_agent.h"
#if defined(TRAIL_MATE_BLUEZ_GIO)
#include <cerrno>
#include <cstring>
#include <sys/random.h>
#include <utility>

namespace platform::linux_runtime::detail
{
namespace
{
constexpr auto path = "/org/trailmate/phone/agent";
constexpr auto xml = R"(<node><interface name='org.bluez.Agent1'>
 <method name='Release'/><method name='Cancel'/>
 <method name='RequestPasskey'><arg type='o' direction='in'/><arg type='u' direction='out'/></method>
 <method name='DisplayPasskey'><arg type='o' direction='in'/><arg type='u' direction='in'/><arg type='q' direction='in'/></method>
 <method name='RequestPinCode'><arg type='o' direction='in'/><arg type='s' direction='out'/></method>
 <method name='DisplayPinCode'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>
 <method name='RequestConfirmation'><arg type='o' direction='in'/><arg type='u' direction='in'/></method>
 <method name='RequestAuthorization'><arg type='o' direction='in'/></method>
 <method name='AuthorizeService'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>
</interface></node>)";
struct Reply
{
    bool done = false;
    GVariant* value = nullptr;
    GError* error = nullptr;
};
struct ContextScope
{
    GMainContext* context;
    explicit ContextScope(GMainContext* value) : context(value) { g_main_context_push_thread_default(context); }
    ~ContextScope() { g_main_context_pop_thread_default(context); }
};
void replied(GObject* source, GAsyncResult* result, gpointer opaque)
{
    auto& reply = *static_cast<Reply*>(opaque);
    reply.value = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &reply.error);
    reply.done = true;
}
bool randomPin(uint32_t& pin)
{
    constexpr uint64_t range = 900000;
    constexpr uint64_t limit = (uint64_t{1} << 32) / range * range;
    for (unsigned attempt = 0; attempt < 8; ++attempt)
    {
        uint32_t value;
        ssize_t count;
        do
        {
            count = ::getrandom(&value, sizeof(value), GRND_NONBLOCK);
        } while (count < 0 && errno == EINTR);
        if (count != sizeof(value)) return false;
        if (value >= limit) continue;
        pin = 100000 + value % range;
        return true;
    }
    return false;
}
} // namespace
BluezPairingAgent::BluezPairingAgent(GDBusConnection* connection, GMainContext* context, std::string owner,
                                     std::string adapter, std::string service, BluezPhoneGattOptions options,
                                     std::function<bool(const PhonePairingDisplay&)> presenter, std::function<bool(const std::string&)> allowed)
    : connection_(connection), context_(context), owner_(std::move(owner)), adapter_(std::move(adapter)),
      service_(std::move(service)), options_(std::move(options)), presenter_(std::move(presenter)), allowed_(std::move(allowed)) {}
BluezPairingAgent::~BluezPairingAgent() { stop(); }
bool BluezPairingAgent::rpc(const char* method, GVariant* parameters)
{
    ContextScope scope(context_);
    Reply reply;
    g_dbus_connection_call(connection_, owner_.c_str(), "/org/bluez", "org.bluez.AgentManager1", method, parameters,
                           G_VARIANT_TYPE_UNIT, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, replied, &reply);
    while (!reply.done) g_main_context_iteration(context_, true);
    const bool success = reply.value;
    if (!success) error_ = reply.error ? reply.error->message : "Bluetooth pairing agent unavailable";
    if (reply.value) g_variant_unref(reply.value);
    g_clear_error(&reply.error);
    return success;
}
bool BluezPairingAgent::start()
{
    ContextScope scope(context_);
    if (!presenter_)
    {
        error_ = "Bluetooth pairing display unavailable";
        return false;
    }
    if (options_.pairing_mode == PhonePairingMode::FixedPin && options_.fixed_pin != 0 &&
        (options_.fixed_pin < 100000 || options_.fixed_pin > 999999))
    {
        error_ = "Invalid fixed Bluetooth PIN";
        return false;
    }
    if (options_.pairing_mode != PhonePairingMode::RandomPin && options_.pairing_mode != PhonePairingMode::FixedPin)
    {
        error_ = "Pairing agent requires a PIN policy";
        return false;
    }
    GError* error = nullptr;
    description_ = g_dbus_node_info_new_for_xml(xml, &error);
    if (!description_)
    {
        error_ = error ? error->message : "Invalid pairing agent schema";
        g_clear_error(&error);
        return false;
    }
    static const GDBusInterfaceVTable table = {method, nullptr, nullptr, {}};
    registration_ = g_dbus_connection_register_object(connection_, path, description_->interfaces[0], &table, this, nullptr, &error);
    if (!registration_)
    {
        error_ = error ? error->message : "Cannot export pairing agent";
        g_clear_error(&error);
        return false;
    }
    if (!rpc("RegisterAgent", g_variant_new("(os)", path, "DisplayOnly"))) return false;
    registered_ = true;
    if (!rpc("RequestDefaultAgent", g_variant_new("(o)", path))) return false;
    active_ = true;
    return true;
}
void BluezPairingAgent::stop()
{
    active_ = false;
    clear();
    if (registered_ && !g_dbus_connection_is_closed(connection_))
        (void)rpc("UnregisterAgent", g_variant_new("(o)", path));
    registered_ = false;
    if (registration_) g_dbus_connection_unregister_object(connection_, registration_);
    registration_ = 0;
    if (description_) g_dbus_node_info_unref(description_);
    description_ = nullptr;
}
void BluezPairingAgent::clear(const std::string& peer)
{
    if (!peer.empty() && peer != peer_) return;
    if (peer_.empty()) return;
    peer_.clear();
    pin_ = 0;
    if (presenter_) try
        {
            (void)presenter_({false, 0, false, options_.local_name});
        }
        catch (...)
        {
        }
}
void BluezPairingAgent::poll()
{
    if (!peer_.empty() && std::chrono::steady_clock::now() >= expires_) clear();
}
bool BluezPairingAgent::display(const std::string& peer, uint32_t pin, bool fixed)
{
    if (pin > 999999 || (!peer_.empty() && peer_ != peer)) return false;
    if (!presenter_({true, pin, fixed, options_.local_name})) return false;
    peer_ = peer;
    pin_ = pin;
    expires_ = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    return true;
}
void BluezPairingAgent::method(GDBusConnection*, const gchar* sender, const gchar*, const gchar*, const gchar* name,
                               GVariant* parameters, GDBusMethodInvocation* invocation, gpointer opaque)
{
    auto& self = *static_cast<BluezPairingAgent*>(opaque);
    try
    {
        self.dispatch(sender, name, parameters, invocation);
    }
    catch (...)
    {
        g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected", "Pairing display failed");
    }
}
void BluezPairingAgent::dispatch(const char* sender, const char* name, GVariant* parameters, GDBusMethodInvocation* invocation)
{
    auto reject = [&](const char* reason)
    { g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected", reason); };
    if (!sender || owner_ != sender)
    {
        g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.NotAuthorized", "Only BlueZ may invoke this agent");
        return;
    }
    if (std::strcmp(name, "Release") == 0)
    {
        active_ = registered_ = false;
        clear();
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }
    if (std::strcmp(name, "Cancel") == 0)
    {
        clear();
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }
    if (!active_)
    {
        reject("Pairing agent unavailable");
        return;
    }
    auto* device = g_variant_get_child_value(parameters, 0);
    const std::string peer = g_variant_get_string(device, nullptr);
    g_variant_unref(device);
    if (peer.compare(0, adapter_.size() + 5, adapter_ + "/dev_") != 0 ||
        (!peer_.empty() && peer_ != peer) || (allowed_ && !allowed_(peer)))
    {
        reject("Another phone owns pairing or this device is outside the selected adapter");
        return;
    }
    if (std::strcmp(name, "RequestPasskey") == 0)
    {
        uint32_t pin = pin_;
        const bool fixed = options_.pairing_mode == PhonePairingMode::FixedPin && options_.fixed_pin >= 100000 && options_.fixed_pin <= 999999;
        if (peer_.empty())
        {
            if (fixed) pin = options_.fixed_pin;
            else if (!randomPin(pin))
            {
                reject("Pairing entropy unavailable");
                return;
            }
        }
        if (!display(peer, pin, fixed))
        {
            reject("Pairing PIN could not be displayed");
            return;
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", pin));
        return;
    }
    if (std::strcmp(name, "DisplayPasskey") == 0)
    {
        const char* ignored;
        guint32 pin;
        guint16 entered;
        g_variant_get(parameters, "(&ouq)", &ignored, &pin, &entered);
        if (entered > 6 || !display(peer, pin, false))
        {
            reject("Invalid or undisplayable pairing PIN");
            return;
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }
    if (std::strcmp(name, "AuthorizeService") == 0)
    {
        const char *ignored, *uuid;
        g_variant_get(parameters, "(&o&s)", &ignored, &uuid);
        if (g_ascii_strcasecmp(uuid, service_.c_str()) != 0)
        {
            reject("Unrelated Bluetooth service");
            return;
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }
    // DisplayOnly has no yes/no input; never silently confirm Just Works or
    // authorize unrelated legacy PIN services through this application.
    reject("Pairing method is not supported by the display-only agent");
}
} // namespace platform::linux_runtime::detail
#endif
