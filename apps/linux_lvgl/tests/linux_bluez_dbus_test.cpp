#include "platform/linux/bluez_adapters.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <future>
#include <gio/gio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace
{
constexpr auto xml = "<node><interface name='org.freedesktop.DBus.ObjectManager'><method name='GetManagedObjects'><arg type='a{oa{sa{sv}}}' direction='out'/></method></interface></node>";
constexpr auto objects = "({'/org/bluez/hci9': {'org.bluez.Adapter1': {'Address': <'AB:CD:EF:01:02:03'>, 'Alias': <'Controller nine'>, 'Powered': <true>}, 'org.bluez.GattManager1': {}, 'org.bluez.LEAdvertisingManager1': {}}, '/org/bluez/hci2': {'org.bluez.Adapter1': {'Address': <'AB:CD:EF:01:02:04'>, 'Powered': <false>}, 'org.bluez.GattManager1': {}, 'org.bluez.LEAdvertisingManager1': {}}, '/org/bluez/hci9/dev_test': {'org.bluez.Device1': {'Address': <'AB:CD:EF:99:99:99'>}}},)";
void method(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant*, GDBusMethodInvocation* invocation, gpointer data)
{
    g_dbus_method_invocation_return_value(invocation, static_cast<GVariant*>(data));
}
void stalledBusDeadline()
{
    GError* error = nullptr;
    auto* directory = g_dir_make_tmp("trailmate-bluez-XXXXXX", &error);
    assert(directory && !error);
    sockaddr_un endpoint{};
    endpoint.sun_family = AF_UNIX;
    const auto length = std::snprintf(endpoint.sun_path, sizeof(endpoint.sun_path), "%s/bus.sock", directory);
    assert(length > 0 && static_cast<size_t>(length) < sizeof(endpoint.sun_path));
    const int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(listener >= 0 && bind(listener, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)) == 0 && listen(listener, 1) == 0);
    std::promise<void> release;
    auto released = release.get_future();
    std::thread peer([&]
                     {
        const int client = accept(listener, nullptr, nullptr);
        assert(client >= 0);
        released.wait();
        close(client); });
    const auto started = std::chrono::steady_clock::now();
    const auto listing = platform::linux_runtime::queryBluezAdapters(std::string("unix:path=") + endpoint.sun_path);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    release.set_value();
    peer.join();
    close(listener);
    unlink(endpoint.sun_path);
    rmdir(directory);
    g_free(directory);
    assert(!listing.queried && !listing.error.empty());
    assert(elapsed >= std::chrono::milliseconds(1500) && elapsed < std::chrono::seconds(4));
}
} // namespace
int main()
{
    using namespace platform::linux_runtime;
    auto* bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(bus);
    const std::string address = g_test_dbus_get_bus_address(bus);
    auto absent = queryBluezAdapters(address);
    assert(!absent.queried && !absent.error.empty());
    std::promise<GMainLoop*> ready;
    auto future = ready.get_future();
    std::thread server([&]
                       {
        auto* context = g_main_context_new();
        g_main_context_push_thread_default(context);
        GError* error = nullptr;
        auto* connection = g_dbus_connection_new_for_address_sync(address.c_str(), static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION), nullptr, nullptr, &error);
        assert(connection && !error);
        auto* description = g_dbus_node_info_new_for_xml(xml, &error);
        assert(description && !error);
        auto* response = g_variant_parse(G_VARIANT_TYPE("(a{oa{sa{sv}}})"), objects, nullptr, nullptr, &error);
        assert(response && !error);
        static const GDBusInterfaceVTable vtable = {method, nullptr, nullptr, {}};
        const auto registration = g_dbus_connection_register_object(connection, "/", description->interfaces[0], &vtable, response, nullptr, &error);
        assert(registration && !error);
        auto* name = g_dbus_connection_call_sync(connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", "org.bluez", 0U), G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error);
        assert(name && !error);
        g_variant_unref(name);
        auto* loop = g_main_loop_new(context, false);
        ready.set_value(loop);
        g_main_loop_run(loop);
        g_dbus_connection_unregister_object(connection, registration);
        g_variant_unref(response);
        g_dbus_node_info_unref(description);
        g_dbus_connection_close_sync(connection, nullptr, nullptr);
        g_object_unref(connection);
        g_main_loop_unref(loop);
        g_main_context_pop_thread_default(context);
        g_main_context_unref(context); });
    auto* loop = future.get();
    const auto listing = queryBluezAdapters(address);
    assert(listing.queried && listing.error.empty() && listing.adapters.size() == 2);
    assert(listing.adapters[0].path == "/org/bluez/hci9" && listing.adapters[0].name == "Controller nine");
    assert(selectBluezAdapter(listing).status == BluezSelectionStatus::Ambiguous);
    assert(selectBluezAdapter(listing, "ab:cd:ef:01:02:03").status == BluezSelectionStatus::Ready);
    assert(selectBluezAdapter(listing, "AB:CD:EF:01:02:04").status == BluezSelectionStatus::PoweredOff);
    g_main_loop_quit(loop);
    server.join();
    assert(!queryBluezAdapters(address).queried);
    assert(!queryBluezAdapters("invalid-address").queried);
    g_test_dbus_down(bus);
    g_object_unref(bus);
    stalledBusDeadline();
}
