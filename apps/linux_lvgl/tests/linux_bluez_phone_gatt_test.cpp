#include "platform/linux/bluez_phone_gatt.h"
#include "platform/shared/ble/phone_ble_uuids.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <future>
#include <gio/gio.h>
#include <mutex>
#include <thread>

using namespace platform::linux_runtime;
namespace
{
constexpr auto root = "/org/trailmate/phone";
constexpr auto service = "/org/trailmate/phone/service";
constexpr auto advert = "/org/trailmate/phone/advertisement";
constexpr auto characteristic = "org.bluez.GattCharacteristic1";
constexpr auto properties = "org.freedesktop.DBus.Properties";
constexpr auto fixture_xml = R"(<node>
 <interface name='org.freedesktop.DBus.ObjectManager'>
  <method name='GetManagedObjects'><arg type='a{oa{sa{sv}}}' direction='out'/></method>
 </interface>
 <interface name='org.bluez.GattManager1'>
  <method name='RegisterApplication'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/></method>
 </interface>
 <interface name='org.bluez.LEAdvertisingManager1'>
  <method name='RegisterAdvertisement'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/></method>
 </interface>
</node>)";
constexpr auto adapters = "({'/org/bluez/hci9': {'org.bluez.Adapter1': {'Address': <'AB:CD:EF:01:02:03'>, 'Powered': <true>}, 'org.bluez.GattManager1': {}, 'org.bluez.LEAdvertisingManager1': {}}},)";
GDBusConnection* connect(const std::string& address)
{
    GError* error = nullptr;
    auto* connection = g_dbus_connection_new_for_address_sync(address.c_str(),
                                                              static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION), nullptr, nullptr, &error);
    assert(connection && !error);
    return connection;
}
struct Response
{
    GVariant* value = nullptr;
    GError* error = nullptr;
    Response() = default;
    Response(const Response&) = delete;
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
Response call(GDBusConnection* connection, const std::string& destination, const std::string& path,
              const char* interface, const char* method, GVariant* parameters)
{
    Response response;
    response.value = g_dbus_connection_call_sync(connection, destination.c_str(), path.c_str(), interface, method,
                                                 parameters, nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &response.error);
    return response;
}
struct Fixture
{
    GDBusConnection* connection = nullptr;
    GMainLoop* loop = nullptr;
    std::thread thread;
    std::mutex mutex;
    std::string app;
    GVariant* schema = nullptr;
    std::atomic<bool> reject_advert{false};
    std::atomic<bool> reject_application{false};
    std::atomic<unsigned> notifications{0};
    std::vector<uint8_t> last_notification;

    struct Inspection
    {
        Fixture* fixture;
        GDBusMethodInvocation* invocation;
        bool advertisement;
    };
    static void inspected(GObject* source, GAsyncResult* result, gpointer opaque)
    {
        std::unique_ptr<Inspection> inspection(static_cast<Inspection*>(opaque));
        GError* error = nullptr;
        auto* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
        assert(reply && !error); // Registration must serve reentrant directory/property calls.
        auto& self = *inspection->fixture;
        if (!inspection->advertisement)
        {
            std::lock_guard<std::mutex> lock(self.mutex);
            if (self.schema) g_variant_unref(self.schema);
            self.schema = g_variant_ref(reply);
        }
        g_variant_unref(reply);
        if ((!inspection->advertisement && self.reject_application) || (inspection->advertisement && self.reject_advert))
            g_dbus_method_invocation_return_dbus_error(inspection->invocation, "org.bluez.Error.Failed", "Injected advertising failure");
        else
            g_dbus_method_invocation_return_value(inspection->invocation, nullptr);
    }
    static void method(GDBusConnection* connection, const gchar* sender, const gchar*, const gchar*, const gchar* method,
                       GVariant* parameters, GDBusMethodInvocation* invocation, gpointer opaque)
    {
        auto& self = *static_cast<Fixture*>(opaque);
        if (std::string(method) == "GetManagedObjects")
        {
            GError* error = nullptr;
            auto* objects = g_variant_parse(G_VARIANT_TYPE("(a{oa{sa{sv}}})"), adapters, nullptr, nullptr, &error);
            assert(objects && !error);
            g_dbus_method_invocation_return_value(invocation, objects);
            g_variant_unref(objects);
            return;
        }
        const bool advertisement = std::string(method) == "RegisterAdvertisement";
        const char* path;
        GVariant* options;
        g_variant_get(parameters, "(&o@a{sv})", &path, &options);
        g_variant_unref(options);
        {
            std::lock_guard<std::mutex> lock(self.mutex);
            self.app = sender;
        }
        if (advertisement) assert(std::string(path) == advert);
        else assert(std::string(path) == root);
        auto* inspection = new Inspection{&self, invocation, advertisement};
        g_dbus_connection_call(connection, sender, path,
                               advertisement ? properties : "org.freedesktop.DBus.ObjectManager",
                               advertisement ? "GetAll" : "GetManagedObjects",
                               advertisement ? g_variant_new("(s)", "org.bluez.LEAdvertisement1") : nullptr,
                               nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, inspected, inspection);
    }
    static void notification(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer opaque)
    {
        auto& self = *static_cast<Fixture*>(opaque);
        const char* interface;
        GVariant *values, *invalidated;
        g_variant_get(parameters, "(&s@a{sv}@as)", &interface, &values, &invalidated);
        if (auto* bytes = g_variant_lookup_value(values, "Value", G_VARIANT_TYPE_BYTESTRING))
        {
            gsize length = 0;
            const auto* begin = static_cast<const uint8_t*>(g_variant_get_fixed_array(bytes, &length, 1));
            std::lock_guard<std::mutex> lock(self.mutex);
            self.last_notification.assign(begin, begin + length);
            ++self.notifications;
            g_variant_unref(bytes);
        }
        g_variant_unref(values);
        g_variant_unref(invalidated);
    }
    explicit Fixture(const std::string& address)
    {
        std::promise<void> ready;
        auto future = ready.get_future();
        thread = std::thread([&, address]
                             {
            auto* context = g_main_context_new();
            g_main_context_push_thread_default(context);
            connection = connect(address);
            GError* error = nullptr;
            auto* description = g_dbus_node_info_new_for_xml(fixture_xml, &error);
            assert(description && !error);
            static const GDBusInterfaceVTable table = {method, nullptr, nullptr, {}};
            std::vector<guint> registrations;
            for (unsigned i = 0; i < 3; ++i)
            {
                auto id = g_dbus_connection_register_object(connection, i == 0 ? "/" : "/org/bluez/hci9",
                    description->interfaces[i], &table, this, nullptr, &error);
                assert(id && !error);
                registrations.push_back(id);
            }
            auto name = call(connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", "org.bluez", 0U));
            assert(name.value && !name.error);
            const auto subscription = g_dbus_connection_signal_subscribe(connection, nullptr, properties, "PropertiesChanged", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, notification, this, nullptr);
            loop = g_main_loop_new(context, false);
            ready.set_value();
            g_main_loop_run(loop);
            g_dbus_connection_signal_unsubscribe(connection, subscription);
            for (auto id : registrations) g_dbus_connection_unregister_object(connection, id);
            g_dbus_node_info_unref(description);
            g_dbus_connection_close_sync(connection, nullptr, nullptr);
            g_object_unref(connection);
            g_main_loop_unref(loop);
            g_main_context_pop_thread_default(context);
            g_main_context_unref(context); });
        future.get();
    }
    ~Fixture()
    {
        g_main_loop_quit(loop);
        thread.join();
        if (schema) g_variant_unref(schema);
    }
    std::string destination()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return app;
    }
    GVariant* characterProperties(unsigned index)
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto* objects = g_variant_get_child_value(schema, 0);
        const auto path = std::string(service) + "/char" + std::to_string(index);
        auto* interfaces = g_variant_lookup_value(objects, path.c_str(), G_VARIANT_TYPE("a{sa{sv}}"));
        assert(interfaces);
        auto* values = g_variant_lookup_value(interfaces, characteristic, G_VARIANT_TYPE_VARDICT);
        g_variant_unref(interfaces);
        g_variant_unref(objects);
        assert(values);
        return values;
    }
};
template <typename F>
auto pumping(BluezPhoneGatt& application, F action)
{
    auto future = std::async(std::launch::async, std::move(action));
    while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) application.poll();
    application.poll();
    return future.get();
}
GVariant* options(const char* peer = "/org/bluez/hci9/dev_AA_BB_CC_DD_EE_FF", uint16_t offset = 0, uint16_t mtu = 23)
{
    GVariantBuilder values;
    g_variant_builder_init(&values, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&values, "{sv}", "device", g_variant_new_object_path(peer));
    g_variant_builder_add(&values, "{sv}", "offset", g_variant_new_uint16(offset));
    g_variant_builder_add(&values, "{sv}", "mtu", g_variant_new_uint16(mtu));
    return g_variant_builder_end(&values);
}
void expectedError(const Response& response, const char* expected)
{
    assert(!response.value && response.error);
    auto* remote = g_dbus_error_get_remote_error(response.error);
    assert(remote && std::string(remote) == expected);
    g_free(remote);
}
void waitNotifications(Fixture& fixture, unsigned count)
{
    for (unsigned i = 0; i < 2000 && fixture.notifications < count; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(fixture.notifications == count);
}
void observe(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer opaque)
{
    const char* interface;
    GVariant *values, *invalidated;
    g_variant_get(parameters, "(&s@a{sv}@as)", &interface, &values, &invalidated);
    if (auto* value = g_variant_lookup_value(values, "Value", G_VARIANT_TYPE_BYTESTRING))
    {
        ++*static_cast<unsigned*>(opaque);
        g_variant_unref(value);
    }
    g_variant_unref(values);
    g_variant_unref(invalidated);
}
void connectionChanged(Fixture& fixture, const char* path, bool connected)
{
    GVariantBuilder changed;
    g_variant_builder_init(&changed, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&changed, "{sv}", "Connected", g_variant_new_boolean(connected));
    assert(g_dbus_connection_emit_signal(fixture.connection, nullptr, path, properties, "PropertiesChanged",
                                         g_variant_new("(s@a{sv}@as)", "org.bluez.Device1", g_variant_builder_end(&changed), g_variant_new_strv(nullptr, 0)), nullptr));
}
int systemProbe(PhoneGattProtocol protocol)
{
    BluezPhoneGatt application;
    BluezPhoneGattOptions configuration;
    configuration.protocol = protocol;
    configuration.local_name = "Trail Mate BLE test";
    if (!application.start(configuration, {}))
    {
        std::fprintf(stderr, "GATT registration failed: %s\n", application.error().c_str());
        return 1;
    }
    std::puts("GATT application and advertisement registered");
    for (unsigned i = 0; i < 200; ++i)
    {
        application.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(application.ready());
    application.stop();
    assert(!application.ready());
    std::puts("Dedicated Bluetooth connection closed; registrations released");
    return 0;
}
} // namespace
int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--system") return systemProbe(PhoneGattProtocol::Meshtastic);
    if (argc == 2 && std::string(argv[1]) == "--system-meshcore") return systemProbe(PhoneGattProtocol::MeshCore);
    auto* bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(bus);
    const std::string address = g_test_dbus_get_bus_address(bus);
    {
        Fixture fixture(address);
        BluezPhoneGatt application;
        BluezPhoneGattOptions configuration;
        configuration.bus_address = address;
        configuration.enable_from_radio_sync = true;
        unsigned connected = 0, disconnected = 0, reads = 0, writes = 0;
        bool accept_write = true;
        BluezPhoneGattHooks hooks;
        hooks.connected = [&](const auto&)
        { ++connected; };
        hooks.disconnected = [&]
        { ++disconnected; };
        hooks.read = [&](auto channel, auto& bytes)
        {
            assert(channel == PhoneGattChannel::FromRadio);
            ++reads;
            bytes.resize(510);
            for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = uint8_t(i);
            return true;
        };
        hooks.write = [&](auto channel, const auto& bytes)
        {
            assert(channel == PhoneGattChannel::ToRadio || channel == PhoneGattChannel::MeshCoreRx);
            assert(bytes == std::vector<uint8_t>({1, 2, 3}));
            ++writes;
            return accept_write;
        };
        assert(application.start(configuration, hooks) && application.ready());
        auto* metadata = fixture.characterProperties(1);
        const char* uuid;
        assert(g_variant_lookup(metadata, "UUID", "&s", &uuid) && std::string(uuid) == FROMRADIO_UUID);
        auto* flags = g_variant_lookup_value(metadata, "Flags", G_VARIANT_TYPE_STRING_ARRAY);
        assert(flags && g_variant_n_children(flags) == 1);
        auto* flag = g_variant_get_child_value(flags, 0);
        assert(std::string(g_variant_get_string(flag, nullptr)) == "encrypt-read");
        g_variant_unref(flag);
        g_variant_unref(flags);
        g_variant_unref(metadata);
        auto invoke = [&](unsigned index, const char* method, GVariant* parameters)
        {
            const auto destination = fixture.destination();
            const auto path = std::string(service) + "/char" + std::to_string(index);
            return pumping(application, [&]
                           { return call(fixture.connection, destination, path, characteristic, method, parameters); });
        };
        auto response = invoke(1, "ReadValue", g_variant_new("(@a{sv})", options()));
        assert(response.value && connected == 1 && reads == 1);
        auto* bytes = g_variant_get_child_value(response.value, 0);
        assert(g_variant_n_children(bytes) == 22);
        g_variant_unref(bytes);
        auto blob = invoke(1, "ReadValue", g_variant_new("(@a{sv})", options("/org/bluez/hci9/dev_AA_BB_CC_DD_EE_FF", 22)));
        assert(blob.value && reads == 1);
        bytes = g_variant_get_child_value(blob.value, 0);
        gsize length;
        const auto* data = static_cast<const uint8_t*>(g_variant_get_fixed_array(bytes, &length, 1));
        assert(length == 22 && data[0] == 22);
        g_variant_unref(bytes);
        expectedError(invoke(1, "ReadValue", g_variant_new("(@a{sv})", options("/org/bluez/hci9/dev_OTHER"))), "org.bluez.Error.InProgress");
        assert(reads == 1);
        const uint8_t payload[] = {1, 2, 3};
        auto write = [&]
        { return g_variant_new("(@ay@a{sv})", g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, payload, 3, 1), options()); };
        assert(invoke(0, "WriteValue", write()).value && writes == 1);
        accept_write = false;
        expectedError(invoke(0, "WriteValue", write()), "org.bluez.Error.Failed");
        auto* unauthorized = connect(address);
        auto* observer_context = g_main_context_new();
        unsigned observed_payloads = 0;
        g_main_context_push_thread_default(observer_context);
        const auto observer = g_dbus_connection_signal_subscribe(unauthorized, nullptr, properties, "PropertiesChanged", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, observe, &observed_payloads, nullptr);
        g_main_context_pop_thread_default(observer_context);
        const auto destination = fixture.destination();
        auto forbidden = pumping(application, [&]
                                 { return call(unauthorized, destination, std::string(service) + "/char0", characteristic, "WriteValue", write()); });
        expectedError(forbidden, "org.bluez.Error.NotAuthorized");
        assert(writes == 2);
        assert(invoke(2, "StartNotify", nullptr).value);
        assert(application.notify(PhoneGattChannel::FromNum, {1, 0, 0, 0}));
        assert(!application.notify(PhoneGattChannel::FromNum, {1}));
        waitNotifications(fixture, 1);
        assert(invoke(4, "StartNotify", nullptr).value);
        assert(!application.notify(PhoneGattChannel::FromRadioSync, std::vector<uint8_t>(21)));
        assert(application.notify(PhoneGattChannel::FromRadioSync, {4, 5}));
        waitNotifications(fixture, 2);
        connectionChanged(fixture, "/org/bluez/hci9/dev_OTHER", true);
        // A call from the same daemon connection orders the preceding device signal.
        assert(invoke(2, "StartNotify", nullptr).value);
        assert(!application.notify(PhoneGattChannel::FromRadioSync, {4, 5}));
        connectionChanged(fixture, "/org/bluez/hci9/dev_OTHER", false);
        assert(invoke(2, "StartNotify", nullptr).value);
        assert(application.notify(PhoneGattChannel::FromRadioSync, {6, 7}));
        waitNotifications(fixture, 3);
        GError* added_error = nullptr;
        auto* added = g_variant_parse(G_VARIANT_TYPE("(oa{sa{sv}})"),
                                      "('/org/bluez/hci9/dev_ADDED', {'org.bluez.Device1': {'Connected': <true>}})", nullptr, nullptr, &added_error);
        assert(added && !added_error);
        assert(g_dbus_connection_emit_signal(fixture.connection, nullptr, "/", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", added, nullptr));
        g_variant_unref(added);
        assert(invoke(2, "StartNotify", nullptr).value);
        assert(!application.notify(PhoneGattChannel::FromRadioSync, {4, 5}));
        connectionChanged(fixture, "/org/bluez/hci9/dev_ADDED", false);
        assert(invoke(2, "StartNotify", nullptr).value);
        // A private Value notification must not be broadcast to other bus clients.
        auto public_value = pumping(application, [&]
                                    { return call(unauthorized, destination, std::string(service) + "/char4", properties, "Get", g_variant_new("(ss)", characteristic, "Value")); });
        assert(public_value.value);
        auto* boxed = g_variant_get_child_value(public_value.value, 0);
        auto* empty = g_variant_get_variant(boxed);
        assert(g_variant_n_children(empty) == 0);
        g_variant_unref(empty);
        g_variant_unref(boxed);
        while (g_main_context_pending(observer_context)) g_main_context_iteration(observer_context, false);
        assert(observed_payloads == 0);
        connectionChanged(fixture, "/org/bluez/hci9/dev_AA_BB_CC_DD_EE_FF", false);
        for (unsigned i = 0; i < 2000 && disconnected == 0; ++i)
        {
            application.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(disconnected == 1 && application.ready());
        assert(!application.notify(PhoneGattChannel::FromNum, {2, 0, 0, 0}));
        assert(invoke(1, "ReadValue", g_variant_new("(@a{sv})", options("/org/bluez/hci9/dev_OTHER"))).value);
        assert(connected == 2 && reads == 2);
        application.stop();
        assert(disconnected == 2 && !application.ready());
        fixture.reject_advert = true;
        assert(!application.start(configuration, hooks) && !application.ready() && !application.error().empty());
        const auto failed_destination = fixture.destination();
        auto alive = call(fixture.connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner", g_variant_new("(s)", failed_destination.c_str()));
        assert(alive.value);
        gboolean has_owner;
        g_variant_get(alive.value, "(b)", &has_owner);
        assert(!has_owner);
        fixture.reject_advert = false;
        fixture.reject_application = true;
        assert(!application.start(configuration, hooks) && !application.ready());
        fixture.reject_application = false;
        configuration.protocol = PhoneGattProtocol::MeshCore;
        configuration.encrypted = false;
        assert(application.start(configuration, hooks));
        metadata = fixture.characterProperties(1);
        assert(g_variant_lookup(metadata, "UUID", "&s", &uuid) && std::string(uuid) == NUS_CHAR_TX_UUID);
        g_variant_unref(metadata);
        accept_write = true;
        assert(invoke(0, "WriteValue", write()).value);
        assert(invoke(1, "StartNotify", nullptr).value);
        assert(!application.notify(PhoneGattChannel::MeshCoreTx, std::vector<uint8_t>(172)));
        assert(invoke(0, "WriteValue", g_variant_new("(@ay@a{sv})", g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, payload, 3, 1), options("/org/bluez/hci9/dev_AA_BB_CC_DD_EE_FF", 0, 185))).value);
        assert(application.notify(PhoneGattChannel::MeshCoreTx, std::vector<uint8_t>(172)));
        waitNotifications(fixture, 4);
        auto release = call(fixture.connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ReleaseName", g_variant_new("(s)", "org.bluez"));
        assert(release.value);
        for (unsigned i = 0; i < 2000 && application.ready(); ++i)
        {
            application.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(!application.ready() && !application.error().empty());
        application.stop();
        g_dbus_connection_signal_unsubscribe(unauthorized, observer);
        g_main_context_unref(observer_context);
        g_dbus_connection_close_sync(unauthorized, nullptr, nullptr);
        g_object_unref(unauthorized);
    }
    g_test_dbus_down(bus);
    g_object_unref(bus);
}
