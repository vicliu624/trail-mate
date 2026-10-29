#include "bluez_pairing_agent.h"
#include "platform/shared/ble/phone_ble_uuids.h"
#include <atomic>
#include <cassert>
#include <future>
#include <mutex>
#include <thread>

using namespace platform::linux_runtime;
using detail::BluezPairingAgent;
using namespace std::chrono_literals;
namespace
{
GDBusConnection* connect(const std::string& address)
{
    GError* error = nullptr;
    auto* result = g_dbus_connection_new_for_address_sync(address.c_str(),
                                                          static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION), nullptr, nullptr, &error);
    assert(result && !error);
    return result;
}
struct Manager
{
    GDBusConnection* connection = nullptr;
    GMainLoop* loop = nullptr;
    std::thread thread;
    std::atomic<bool> reject_default{false};
    std::atomic<unsigned> registrations{0}, removals{0};
    static void method(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* name,
                       GVariant* parameters, GDBusMethodInvocation* invocation, gpointer opaque)
    {
        auto& self = *static_cast<Manager*>(opaque);
        if (std::string(name) == "RegisterAgent")
        {
            const char *path, *capability;
            g_variant_get(parameters, "(&o&s)", &path, &capability);
            assert(std::string(path) == "/org/trailmate/phone/agent" && std::string(capability) == "DisplayOnly");
            ++self.registrations;
        }
        else if (std::string(name) == "UnregisterAgent") ++self.removals;
        else if (self.reject_default)
        {
            g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected", "Injected default-agent denial");
            return;
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
    }
    explicit Manager(const std::string& address)
    {
        std::promise<void> ready;
        auto future = ready.get_future();
        thread = std::thread([&, address]
                             {
            auto* context = g_main_context_new();
            g_main_context_push_thread_default(context);
            connection = connect(address);
            GError* error = nullptr;
            constexpr auto xml = "<node><interface name='org.bluez.AgentManager1'><method name='RegisterAgent'><arg type='o' direction='in'/><arg type='s' direction='in'/></method><method name='RequestDefaultAgent'><arg type='o' direction='in'/></method><method name='UnregisterAgent'><arg type='o' direction='in'/></method></interface></node>";
            auto* info = g_dbus_node_info_new_for_xml(xml, &error);
            assert(info && !error);
            static const GDBusInterfaceVTable table = {method, nullptr, nullptr, {}};
            const auto id = g_dbus_connection_register_object(connection, "/org/bluez", info->interfaces[0], &table, this, nullptr, &error);
            assert(id && !error);
            auto* reply = g_dbus_connection_call_sync(connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", "org.bluez", 0U), nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error);
            assert(reply && !error);
            g_variant_unref(reply);
            loop = g_main_loop_new(context, false);
            ready.set_value();
            g_main_loop_run(loop);
            g_dbus_connection_unregister_object(connection, id);
            g_dbus_node_info_unref(info);
            g_dbus_connection_close_sync(connection, nullptr, nullptr);
            g_object_unref(connection);
            g_main_loop_unref(loop);
            g_main_context_pop_thread_default(context);
            g_main_context_unref(context); });
        future.get();
    }
    ~Manager()
    {
        g_main_loop_quit(loop);
        thread.join();
    }
};
struct Response
{
    GVariant* value = nullptr;
    GError* error = nullptr;
    Response() = default;
    Response(Response&& other) : value(other.value), error(other.error)
    {
        other.value = nullptr;
        other.error = nullptr;
    }
    ~Response()
    {
        if (value) g_variant_unref(value);
        g_clear_error(&error);
    }
};
Response invoke(GMainContext* context, GDBusConnection* caller, const char* destination, const char* method, GVariant* parameters)
{
    auto future = std::async(std::launch::async, [&]
                             {
        Response result;
        result.value = g_dbus_connection_call_sync(caller, destination, "/org/trailmate/phone/agent", "org.bluez.Agent1", method, parameters, nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &result.error);
        return result; });
    while (future.wait_for(1ms) != std::future_status::ready)
        while (g_main_context_pending(context)) g_main_context_iteration(context, false);
    return future.get();
}
} // namespace
int main()
{
    auto* bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(bus);
    const std::string address = g_test_dbus_get_bus_address(bus);
    {
        Manager manager(address);
        auto* context = g_main_context_new();
        g_main_context_push_thread_default(context);
        auto* connection = connect(address);
        const auto* destination = g_dbus_connection_get_unique_name(connection);
        const auto owner = std::string(g_dbus_connection_get_unique_name(manager.connection));
        PhonePairingDisplay shown;
        bool accept_display = true;
        auto presenter = [&](const PhonePairingDisplay& value)
        { if (!accept_display && value.active) return false; shown = value; return true; };
        BluezPhoneGattOptions options;
        options.pairing_mode = PhonePairingMode::FixedPin;
        options.fixed_pin = 123456;
        {
            BluezPairingAgent agent(connection, context, owner, "/org/bluez/hci7", MESH_SERVICE_UUID, options, presenter, [](const auto&)
                                    { return true; });
            assert(agent.start() && agent.ready());
            auto requested = invoke(context, manager.connection, destination, "RequestPasskey", g_variant_new("(o)", "/org/bluez/hci7/dev_A"));
            guint32 pin;
            assert(requested.value && shown.active && shown.fixed && shown.passkey == 123456);
            g_variant_get(requested.value, "(u)", &pin);
            assert(pin == 123456);
            auto again = invoke(context, manager.connection, destination, "RequestPasskey", g_variant_new("(o)", "/org/bluez/hci7/dev_A"));
            assert(again.value && shown.passkey == 123456);
            assert(invoke(context, manager.connection, destination, "RequestPasskey", g_variant_new("(o)", "/org/bluez/hci7/dev_B")).error);
            assert(invoke(context, manager.connection, destination, "RequestPasskey", g_variant_new("(o)", "/org/bluez/hci0/dev_A")).error);
            assert(invoke(context, connection, destination, "RequestPasskey", g_variant_new("(o)", "/org/bluez/hci7/dev_A")).error);
            assert(invoke(context, manager.connection, destination, "AuthorizeService", g_variant_new("(os)", "/org/bluez/hci7/dev_A", NUS_SERVICE_UUID)).error);
            assert(invoke(context, manager.connection, destination, "AuthorizeService", g_variant_new("(os)", "/org/bluez/hci7/dev_A", MESH_SERVICE_UUID)).value);
            assert(invoke(context, manager.connection, destination, "RequestConfirmation", g_variant_new("(ou)", "/org/bluez/hci7/dev_A", 123456U)).error);
            assert(invoke(context, manager.connection, destination, "Cancel", nullptr).value && !shown.active);
            assert(invoke(context, manager.connection, destination, "DisplayPasskey", g_variant_new("(ouq)", "/org/bluez/hci7/dev_B", 0U, 0)).value);
            assert(shown.active && shown.passkey == 0); // Zero is a valid six-digit display value.
            agent.clear("/org/bluez/hci7/dev_A");
            assert(shown.active);
            agent.clear("/org/bluez/hci7/dev_B");
            assert(!shown.active);
            accept_display = false;
            assert(invoke(context, manager.connection, destination, "RequestPasskey", g_variant_new("(o)", "/org/bluez/hci7/dev_A")).error);
            accept_display = true;
        }
        assert(manager.removals == 1);
        options.pairing_mode = PhonePairingMode::RandomPin;
        {
            BluezPairingAgent agent(connection, context, owner, "/org/bluez/hci7", MESH_SERVICE_UUID, options, presenter, {});
            assert(agent.start());
            assert(invoke(context, manager.connection, destination, "RequestPasskey", g_variant_new("(o)", "/org/bluez/hci7/dev_A")).value);
            assert(shown.active && !shown.fixed && shown.passkey >= 100000 && shown.passkey <= 999999);
        }
        manager.reject_default = true;
        {
            BluezPairingAgent agent(connection, context, owner, "/org/bluez/hci7", MESH_SERVICE_UUID, options, presenter, {});
            assert(!agent.start() && !agent.ready() && !agent.error().empty());
        }
        assert(manager.registrations == 3 && manager.removals == 3 && !shown.active);
        g_dbus_connection_close_sync(connection, nullptr, nullptr);
        g_object_unref(connection);
        g_main_context_pop_thread_default(context);
        g_main_context_unref(context);
    }
    g_test_dbus_down(bus);
    g_object_unref(bus);
}
