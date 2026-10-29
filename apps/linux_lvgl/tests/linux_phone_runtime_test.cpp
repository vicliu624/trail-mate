#include "app/linux_app_services.h"
#include "meshtastic/admin.pb.h"
#include "meshtastic/mesh.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <gio/gio.h>
#include <mutex>
#include <thread>
#include <unistd.h>

using namespace trailmate::linux_app;
using namespace std::chrono_literals;
namespace
{
constexpr auto adapter = "/org/bluez/hci7";
constexpr auto device = "/org/bluez/hci7/dev_PHONE";
constexpr auto service = "/org/trailmate/phone/service";
constexpr auto characteristic = "org.bluez.GattCharacteristic1";
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
 <interface name='org.bluez.Device1'><method name='Disconnect'/></interface>
 <interface name='org.bluez.AgentManager1'>
  <method name='RegisterAgent'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>
  <method name='RequestDefaultAgent'><arg type='o' direction='in'/></method>
  <method name='UnregisterAgent'><arg type='o' direction='in'/></method>
 </interface>
</node>)";
struct Daemon
{
    GDBusConnection* connection = nullptr;
    GMainLoop* loop = nullptr;
    std::thread thread;
    std::mutex mutex;
    std::string destination;
    std::atomic<unsigned> disconnects{0};
    static void method(GDBusConnection* connection, const gchar* sender, const gchar*, const gchar*, const gchar* name,
                       GVariant*, GDBusMethodInvocation* invocation, gpointer opaque)
    {
        auto& self = *static_cast<Daemon*>(opaque);
        if (std::string(name) == "GetManagedObjects")
        {
            GError* error = nullptr;
            auto* objects = g_variant_parse(G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
                                            "({'/org/bluez/hci7': {'org.bluez.Adapter1': {'Address': <'02:13:24:35:46:57'>, 'Powered': <true>}, 'org.bluez.GattManager1': {}, 'org.bluez.LEAdvertisingManager1': {}}},)",
                                            nullptr, nullptr, &error);
            assert(objects && !error);
            g_dbus_method_invocation_return_value(invocation, objects);
            g_variant_unref(objects);
            return;
        }
        if (std::string(name) == "RegisterApplication")
        {
            std::lock_guard<std::mutex> lock(self.mutex);
            self.destination = sender;
        }
        if (std::string(name) == "Disconnect")
        {
            ++self.disconnects;
            GVariantBuilder values;
            g_variant_builder_init(&values, G_VARIANT_TYPE_VARDICT);
            g_variant_builder_add(&values, "{sv}", "Connected", g_variant_new_boolean(false));
            assert(g_dbus_connection_emit_signal(connection, nullptr, device, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                                 g_variant_new("(s@a{sv}@as)", "org.bluez.Device1", g_variant_builder_end(&values), g_variant_new_strv(nullptr, 0)), nullptr));
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
    }
    explicit Daemon(const std::string& address)
    {
        std::promise<void> ready;
        auto future = ready.get_future();
        thread = std::thread([&, address]
                             {
            auto* context = g_main_context_new();
            g_main_context_push_thread_default(context);
            GError* error = nullptr;
            connection = g_dbus_connection_new_for_address_sync(address.c_str(),
                static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION), nullptr, nullptr, &error);
            assert(connection && !error);
            auto* info = g_dbus_node_info_new_for_xml(fixture_xml, &error);
            assert(info && !error);
            static const GDBusInterfaceVTable table = {method, nullptr, nullptr, {}};
            std::vector<guint> registrations;
            for (unsigned i = 0; i < 5; ++i)
            {
                auto id = g_dbus_connection_register_object(connection, i == 0 ? "/" : i == 3 ? device : i == 4 ? "/org/bluez" : adapter, info->interfaces[i], &table, this, nullptr, &error);
                assert(id && !error);
                registrations.push_back(id);
            }
            auto* reply = g_dbus_connection_call_sync(connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
                g_variant_new("(su)", "org.bluez", 0U), nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error);
            assert(reply && !error);
            g_variant_unref(reply);
            loop = g_main_loop_new(context, false);
            ready.set_value();
            g_main_loop_run(loop);
            for (auto id : registrations) g_dbus_connection_unregister_object(connection, id);
            g_dbus_node_info_unref(info);
            g_dbus_connection_close_sync(connection, nullptr, nullptr);
            g_object_unref(connection);
            g_main_loop_unref(loop);
            g_main_context_pop_thread_default(context);
            g_main_context_unref(context); });
        future.get();
    }
    ~Daemon()
    {
        g_main_loop_quit(loop);
        thread.join();
    }
    std::string application()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return destination;
    }
};
std::vector<uint8_t> encode(const pb_msgdesc_t* type, const void* message, size_t maximum)
{
    std::vector<uint8_t> bytes(maximum);
    auto stream = pb_ostream_from_buffer(bytes.data(), bytes.size());
    assert(pb_encode(&stream, type, message));
    bytes.resize(stream.bytes_written);
    return bytes;
}
GVariant* options()
{
    GVariantBuilder values;
    g_variant_builder_init(&values, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&values, "{sv}", "device", g_variant_new_object_path(device));
    g_variant_builder_add(&values, "{sv}", "mtu", g_variant_new_uint16(517));
    return g_variant_builder_end(&values);
}
GVariant* invoke(LinuxAppServices& runtime, Daemon& daemon, unsigned index, const char* method, GVariant* parameters)
{
    const auto destination = daemon.application();
    const auto path = std::string(service) + "/char" + std::to_string(index);
    auto future = std::async(std::launch::async, [&]
                             {
        GError* error = nullptr;
        auto* reply = g_dbus_connection_call_sync(daemon.connection, destination.c_str(), path.c_str(), characteristic, method, parameters,
            nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
        if (error) g_printerr("%s failed: %s\n", method, error->message);
        assert(reply && !error);
        return reply; });
    while (future.wait_for(1ms) != std::future_status::ready) runtime.tick();
    return future.get();
}
void write(LinuxAppServices& runtime, Daemon& daemon, const meshtastic_ToRadio& message)
{
    auto bytes = encode(meshtastic_ToRadio_fields, &message, meshtastic_ToRadio_size);
    auto* reply = invoke(runtime, daemon, 0, "WriteValue",
                         g_variant_new("(@ay@a{sv})", g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, bytes.data(), bytes.size(), 1), options()));
    g_variant_unref(reply);
}
GVariant* agentCall(LinuxAppServices& runtime, Daemon& daemon, const char* method, GVariant* parameters)
{
    const auto destination = daemon.application();
    auto future = std::async(std::launch::async, [&]
                             {
        GError* error = nullptr;
        auto* reply = g_dbus_connection_call_sync(daemon.connection, destination.c_str(), "/org/trailmate/phone/agent", "org.bluez.Agent1", method,
            parameters, nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
        if (error) g_printerr("%s failed: %s\n", method, error->message);
        assert(reply && !error);
        return reply; });
    while (future.wait_for(1ms) != std::future_status::ready) runtime.tick();
    return future.get();
}
} // namespace
int main()
{
    char temporary[] = "/tmp/trailmate-phone-runtime-XXXXXX";
    assert(mkdtemp(temporary));
    setenv("TRAIL_MATE_SETTINGS_ROOT", temporary, 1);
    setenv("TRAIL_MATE_CACHE_ROOT", temporary, 1);
    setenv("TRAIL_MATE_SD_ROOT", temporary, 1);
    const auto absent_radio = std::filesystem::path(temporary) / "no-radio";
    setenv("TRAIL_MATE_LORA_SPI", absent_radio.c_str(), 1);
    setenv("TRAIL_MATE_DEMO_WORLD", "0", 1);
    auto* bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(bus);
    {
        Daemon daemon(g_test_dbus_get_bus_address(bus));
        platform::linux_runtime::PhonePairingDisplay display;
        LinuxAppServicesOptions application_options;
        application_options.runtime_mode = platform::linux_runtime::LinuxRuntimeMode::DeviceLocal;
        application_options.ble_supported = true;
        application_options.phone_ble.bus_address = g_test_dbus_get_bus_address(bus);
        application_options.phone_pairing_presenter = [&](const auto& value)
        { display = value; return true; };
        LinuxAppServices services(application_options);
        assert(services.initialize());
        auto& runtime = services;
        // Production defaults to Reticulum. Never advertise a Meshtastic phone
        // service until the actual application protocol is selected explicitly.
        runtime.setBleEnabled(true);
        for (unsigned i = 0; i < 30; ++i)
        {
            runtime.tick();
            std::this_thread::sleep_for(1ms);
        }
        assert(!runtime.isBleEnabled() && daemon.application().empty());
        assert(services.switchMeshProtocol(chat::MeshProtocol::Meshtastic, false));
        runtime.setBleEnabled(true);
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!runtime.isBleEnabled() && std::chrono::steady_clock::now() < deadline)
        {
            runtime.tick();
            std::this_thread::sleep_for(1ms);
        }
        assert(runtime.isBleEnabled());
        auto* passkey = agentCall(runtime, daemon, "RequestPasskey", g_variant_new("(o)", device));
        guint32 pin;
        g_variant_get(passkey, "(u)", &pin);
        g_variant_unref(passkey);
        assert(display.active && display.passkey == pin && pin >= 100000 && pin <= 999999);
        GVariantBuilder paired;
        g_variant_builder_init(&paired, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&paired, "{sv}", "Paired", g_variant_new_boolean(true));
        assert(g_dbus_connection_emit_signal(daemon.connection, nullptr, device, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                             g_variant_new("(s@a{sv}@as)", "org.bluez.Device1", g_variant_builder_end(&paired), g_variant_new_strv(nullptr, 0)), nullptr));
        auto* subscription = invoke(runtime, daemon, 2, "StartNotify", nullptr);
        g_variant_unref(subscription);
        meshtastic_ToRadio request = meshtastic_ToRadio_init_zero;
        request.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
        request.want_config_id = 0x12345678;
        write(runtime, daemon, request);
        assert(!display.active);
        bool completed = false, self_seen = false;
        for (unsigned i = 0; i < 200 && !completed; ++i)
        {
            auto* reply = invoke(runtime, daemon, 1, "ReadValue", g_variant_new("(@a{sv})", options()));
            auto* bytes = g_variant_get_child_value(reply, 0);
            gsize length;
            const auto* data = static_cast<const uint8_t*>(g_variant_get_fixed_array(bytes, &length, 1));
            meshtastic_FromRadio frame = meshtastic_FromRadio_init_zero;
            auto stream = pb_istream_from_buffer(data, length);
            assert(length && pb_decode(&stream, meshtastic_FromRadio_fields, &frame));
            if (frame.which_payload_variant == meshtastic_FromRadio_my_info_tag)
            {
                assert(frame.my_info.my_node_num == services.selfNodeId());
                self_seen = true;
            }
            if (frame.which_payload_variant == meshtastic_FromRadio_config_complete_id_tag)
            {
                assert(frame.config_complete_id == request.want_config_id);
                completed = true;
            }
            g_variant_unref(bytes);
            g_variant_unref(reply);
        }
        assert(completed && self_seen && runtime.isBleEnabled());
        meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
        admin.which_payload_variant = meshtastic_AdminMessage_set_owner_tag;
        std::strcpy(admin.set_owner.long_name, "Configured across D-Bus");
        std::strcpy(admin.set_owner.short_name, "RPC");
        auto bytes = encode(meshtastic_AdminMessage_fields, &admin, meshtastic_AdminMessage_size);
        request = meshtastic_ToRadio_init_zero;
        request.which_payload_variant = meshtastic_ToRadio_packet_tag;
        request.packet.id = 762;
        request.packet.to = services.selfNodeId();
        request.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        request.packet.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
        request.packet.decoded.payload.size = bytes.size();
        std::copy(bytes.begin(), bytes.end(), request.packet.decoded.payload.bytes);
        write(runtime, daemon, request);
        assert(std::string(services.config().node_name) == "Configured across D-Bus");
        bool drained = false;
        for (unsigned i = 0; i < 100 && !drained; ++i)
        {
            auto* reply = invoke(runtime, daemon, 1, "ReadValue", g_variant_new("(@a{sv})", options()));
            auto* value = g_variant_get_child_value(reply, 0);
            drained = g_variant_n_children(value) == 0;
            g_variant_unref(value);
            g_variant_unref(reply);
        }
        assert(drained);
        const auto persist_until = std::chrono::steady_clock::now() + 350ms;
        while (std::chrono::steady_clock::now() < persist_until)
        {
            runtime.tick();
            std::this_thread::sleep_for(1ms);
        }
        request = meshtastic_ToRadio_init_zero;
        request.which_payload_variant = meshtastic_ToRadio_disconnect_tag;
        request.disconnect = true;
        write(runtime, daemon, request);
        const auto disconnected_at = std::chrono::steady_clock::now() + 5s;
        while (!daemon.disconnects && std::chrono::steady_clock::now() < disconnected_at)
        {
            runtime.tick();
            std::this_thread::sleep_for(1ms);
        }
        assert(daemon.disconnects == 1);
        runtime.setBleEnabled(false);
        runtime.tick();
        assert(!runtime.isBleEnabled());
        const auto before = std::chrono::steady_clock::now();
        runtime.shutdown();
        assert(std::chrono::steady_clock::now() - before < 2s);
        const auto old_destination = daemon.application();
        GError* error = nullptr;
        auto* owner = g_dbus_connection_call_sync(daemon.connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner",
                                                  g_variant_new("(s)", old_destination.c_str()), G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error);
        assert(owner && !error);
        gboolean alive = true;
        g_variant_get(owner, "(b)", &alive);
        g_variant_unref(owner);
        assert(!alive);
        services.shutdown();
    }
    {
        LinuxAppServicesOptions options;
        options.runtime_mode = platform::linux_runtime::LinuxRuntimeMode::DeviceLocal;
        options.ble_supported = true;
        LinuxAppServices reopened(options);
        assert(reopened.initialize());
        assert(std::string(reopened.config().node_name) == "Configured across D-Bus");
        assert(std::string(reopened.config().short_name) == "RPC");
        assert(!reopened.config().ble_enabled && !reopened.isBleEnabled());
    }
    g_test_dbus_down(bus);
    g_object_unref(bus);
    std::filesystem::remove_all(temporary);
}
