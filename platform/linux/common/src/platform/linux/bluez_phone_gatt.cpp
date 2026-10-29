#include "platform/linux/bluez_phone_gatt.h"
#include "bluez_bus.h"
#include "bluez_pairing_agent.h"
#include "platform/linux/bluez_adapters.h"
#include "platform/linux/phone_gatt_session.h"
#include "platform/shared/ble/phone_ble_uuids.h"
#include <array>
#include <cstring>
#include <set>
#include <utility>

namespace platform::linux_runtime
{
#if defined(TRAIL_MATE_BLUEZ_GIO)
namespace
{
constexpr auto root = "/org/trailmate/phone";
constexpr auto service_path = "/org/trailmate/phone/service";
constexpr auto advert_path = "/org/trailmate/phone/advertisement";
constexpr auto object_interface = "org.freedesktop.DBus.ObjectManager";
constexpr auto char_interface = "org.bluez.GattCharacteristic1";
constexpr auto service_interface = "org.bluez.GattService1";
constexpr auto advert_interface = "org.bluez.LEAdvertisement1";
constexpr auto properties_interface = "org.freedesktop.DBus.Properties";
constexpr auto xml = R"(<node>
 <interface name='org.freedesktop.DBus.ObjectManager'>
  <method name='GetManagedObjects'><arg type='a{oa{sa{sv}}}' direction='out'/></method>
 </interface>
 <interface name='org.bluez.GattService1'>
  <property name='UUID' type='s' access='read'/><property name='Primary' type='b' access='read'/>
 </interface>
 <interface name='org.bluez.GattCharacteristic1'>
  <method name='ReadValue'><arg type='a{sv}' direction='in'/><arg type='ay' direction='out'/></method>
  <method name='WriteValue'><arg type='ay' direction='in'/><arg type='a{sv}' direction='in'/></method>
  <method name='StartNotify'/><method name='StopNotify'/>
  <property name='UUID' type='s' access='read'/><property name='Service' type='o' access='read'/>
  <property name='Flags' type='as' access='read'/><property name='Notifying' type='b' access='read'/>
  <property name='Value' type='ay' access='read'/>
 </interface>
 <interface name='org.bluez.LEAdvertisement1'>
  <method name='Release'/><property name='Type' type='s' access='read'/>
  <property name='ServiceUUIDs' type='as' access='read'/><property name='LocalName' type='s' access='read'/>
 </interface>
</node>)";

struct ContextScope
{
    GMainContext* context;
    explicit ContextScope(GMainContext* value) : context(value) { g_main_context_push_thread_default(context); }
    ~ContextScope() { g_main_context_pop_thread_default(context); }
};
struct Reply
{
    bool done = false;
    GVariant* value = nullptr;
    GError* error = nullptr;
};
void replied(GObject* source, GAsyncResult* result, gpointer data)
{
    auto& reply = *static_cast<Reply*>(data);
    reply.value = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &reply.error);
    reply.done = true;
}
GVariant* byteArray(const std::vector<uint8_t>& bytes)
{
    return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, bytes.data(), bytes.size(), 1);
}
const char* resultError(PhoneGattSession::Result result)
{
    switch (result)
    {
    case PhoneGattSession::Result::InvalidOffset:
        return "org.bluez.Error.InvalidOffset";
    case PhoneGattSession::Result::InvalidLength:
        return "org.bluez.Error.InvalidValueLength";
    case PhoneGattSession::Result::Busy:
        return "org.bluez.Error.InProgress";
    default:
        return "org.bluez.Error.Failed";
    }
}
} // namespace
#endif

struct BluezPhoneGatt::Impl
{
    std::string failure;
    bool active = false;
#if defined(TRAIL_MATE_BLUEZ_GIO)
    struct Characteristic
    {
        PhoneGattChannel channel;
        const char* uuid;
        bool readable, writable, notifiable, indicate;
        std::string path;
        bool subscribed = false;
        PhoneGattSession framing;
        Characteristic(PhoneGattChannel id, const char* value, bool r, bool w, bool n, bool i, size_t read_limit, size_t write_limit)
            : channel(id), uuid(value), readable(r), writable(w), notifiable(n), indicate(i), framing(read_limit, write_limit) {}
    };
    BluezPhoneGattOptions options;
    BluezPhoneGattHooks hooks;
    GMainContext* context = nullptr;
    GDBusConnection* connection = nullptr;
    std::unique_ptr<detail::BluezPairingAgent> agent;
    GDBusNodeInfo* description = nullptr;
    std::vector<guint> registrations;
    guint device_subscription = 0, removed_subscription = 0, added_subscription = 0, owner_subscription = 0;
    std::vector<Characteristic> characteristics;
    std::string owner, adapter, peer;
    std::set<std::string> connected_devices;
    uint16_t mtu = 23;

    const char* serviceUuid() const
    {
        return options.protocol == PhoneGattProtocol::Meshtastic ? MESH_SERVICE_UUID : NUS_SERVICE_UUID;
    }
    Characteristic* find(const char* path)
    {
        for (auto& item : characteristics)
            if (item.path == path) return &item;
        return nullptr;
    }
    GVariant* flags(const Characteristic& item) const
    {
        GVariantBuilder values;
        g_variant_builder_init(&values, G_VARIANT_TYPE_STRING_ARRAY);
        const bool authenticated = options.pairing_mode == PhonePairingMode::RandomPin || options.pairing_mode == PhonePairingMode::FixedPin;
        if (item.readable) g_variant_builder_add(&values, "s", authenticated ? "encrypt-authenticated-read" : options.encrypted ? "encrypt-read"
                                                                                                                                : "read");
        if (item.writable)
        {
            g_variant_builder_add(&values, "s", authenticated ? "encrypt-authenticated-write" : options.encrypted ? "encrypt-write"
                                                                                                                  : "write");
            // Both phone protocols support complete write commands as well as requests.
            g_variant_builder_add(&values, "s", "write-without-response");
        }
        if (item.notifiable) g_variant_builder_add(&values, "s", authenticated ? "encrypt-authenticated-notify" : options.encrypted ? "encrypt-notify"
                                                                                                                                    : "notify");
        if (item.indicate) g_variant_builder_add(&values, "s", authenticated ? "encrypt-authenticated-indicate" : options.encrypted ? "encrypt-indicate"
                                                                                                                                    : "indicate");
        return g_variant_builder_end(&values);
    }
    GVariant* properties(const char* path, const char* interface)
    {
        GVariantBuilder values;
        g_variant_builder_init(&values, G_VARIANT_TYPE_VARDICT);
        if (std::strcmp(interface, service_interface) == 0)
        {
            g_variant_builder_add(&values, "{sv}", "UUID", g_variant_new_string(serviceUuid()));
            g_variant_builder_add(&values, "{sv}", "Primary", g_variant_new_boolean(true));
        }
        else if (std::strcmp(interface, advert_interface) == 0)
        {
            const char* uuids[] = {serviceUuid(), nullptr};
            g_variant_builder_add(&values, "{sv}", "Type", g_variant_new_string("peripheral"));
            g_variant_builder_add(&values, "{sv}", "ServiceUUIDs", g_variant_new_strv(uuids, 1));
            g_variant_builder_add(&values, "{sv}", "LocalName", g_variant_new_string(options.local_name.c_str()));
        }
        else if (auto* item = find(path))
        {
            g_variant_builder_add(&values, "{sv}", "UUID", g_variant_new_string(item->uuid));
            g_variant_builder_add(&values, "{sv}", "Service", g_variant_new_object_path(service_path));
            g_variant_builder_add(&values, "{sv}", "Flags", flags(*item));
            g_variant_builder_add(&values, "{sv}", "Notifying", g_variant_new_boolean(item->subscribed));
            // Do not publish protocol payloads through ObjectManager or Properties.Get.
            g_variant_builder_add(&values, "{sv}", "Value", byteArray({}));
        }
        return g_variant_builder_end(&values);
    }
    GVariant* managedObjects()
    {
        GVariantBuilder objects;
        g_variant_builder_init(&objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
        auto add = [&](const char* path, const char* interface)
        {
            GVariantBuilder interfaces;
            g_variant_builder_init(&interfaces, G_VARIANT_TYPE("a{sa{sv}}"));
            g_variant_builder_add(&interfaces, "{s@a{sv}}", interface, properties(path, interface));
            g_variant_builder_add(&objects, "{o@a{sa{sv}}}", path, g_variant_builder_end(&interfaces));
        };
        add(service_path, service_interface);
        for (auto& item : characteristics) add(item.path.c_str(), char_interface);
        return g_variant_new("(@a{oa{sa{sv}}})", g_variant_builder_end(&objects));
    }
    void disconnected()
    {
        const bool had_peer = !peer.empty();
        for (auto& item : characteristics)
        {
            item.framing.disconnect(peer);
            if (item.subscribed)
            {
                item.subscribed = false;
                if (hooks.subscription) try
                    {
                        hooks.subscription(item.channel, false);
                    }
                    catch (...)
                    {
                    }
            }
        }
        peer.clear();
        mtu = 23;
        if (had_peer && hooks.disconnected) try
            {
                hooks.disconnected();
            }
            catch (...)
            {
            }
    }
    void unavailable(const std::string& reason)
    {
        active = false;
        failure = reason;
        if (agent) agent->clear();
        disconnected();
    }
    bool call(const char* interface, const char* method, const char* object)
    {
        Reply reply;
        g_dbus_connection_call(connection, owner.c_str(), adapter.c_str(), interface, method,
                               g_variant_new("(o@a{sv})", object, g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0)),
                               G_VARIANT_TYPE_UNIT, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, replied, &reply);
        while (!reply.done) g_main_context_iteration(context, true);
        const bool ok = reply.value != nullptr;
        if (!ok) failure = reply.error ? reply.error->message : "BlueZ registration failed";
        if (reply.value) g_variant_unref(reply.value);
        g_clear_error(&reply.error);
        return ok;
    }
    bool changed(Characteristic& item, const char* property, GVariant* value)
    {
        GVariantBuilder values;
        g_variant_builder_init(&values, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&values, "{sv}", property, value);
        return g_dbus_connection_emit_signal(connection, owner.c_str(), item.path.c_str(), properties_interface, "PropertiesChanged",
                                             g_variant_new("(s@a{sv}@as)", char_interface, g_variant_builder_end(&values), g_variant_new_strv(nullptr, 0)), nullptr);
    }
    static void signal(GDBusConnection*, const gchar*, const gchar* path, const gchar*, const gchar* name, GVariant* parameters, gpointer opaque)
    {
        auto& self = *static_cast<Impl*>(opaque);
        if (std::strcmp(name, "NameOwnerChanged") == 0)
        {
            const char *service, *old_owner, *new_owner;
            g_variant_get(parameters, "(&s&s&s)", &service, &old_owner, &new_owner);
            if (self.owner == old_owner && self.owner != new_owner) self.unavailable("BlueZ service owner changed");
        }
        else if (std::strcmp(name, "InterfacesAdded") == 0)
        {
            const char* added;
            GVariant* interfaces;
            g_variant_get(parameters, "(&o@a{sa{sv}})", &added, &interfaces);
            if (std::string(added).compare(0, self.adapter.size() + 5, self.adapter + "/dev_") == 0)
            {
                if (auto* values = g_variant_lookup_value(interfaces, "org.bluez.Device1", G_VARIANT_TYPE_VARDICT))
                {
                    gboolean connected = false;
                    if (g_variant_lookup(values, "Connected", "b", &connected) && connected) self.connected_devices.insert(added);
                    g_variant_unref(values);
                }
            }
            g_variant_unref(interfaces);
        }
        else if (std::strcmp(name, "InterfacesRemoved") == 0)
        {
            const char* removed;
            GVariant* interfaces;
            g_variant_get(parameters, "(&o@as)", &removed, &interfaces);
            self.connected_devices.erase(removed);
            if (self.agent) self.agent->clear(removed);
            if (self.peer == removed) self.disconnected();
            if (self.adapter == removed) self.unavailable("Bluetooth adapter removed");
            g_variant_unref(interfaces);
        }
        else
        {
            const char* interface;
            GVariant *values, *invalidated;
            g_variant_get(parameters, "(&s@a{sv}@as)", &interface, &values, &invalidated);
            gboolean state;
            if (self.agent && std::strcmp(interface, "org.bluez.Device1") == 0 &&
                g_variant_lookup(values, "Paired", "b", &state) && state) self.agent->clear(path);
            if (std::string(path).compare(0, self.adapter.size() + 5, self.adapter + "/dev_") == 0 &&
                std::strcmp(interface, "org.bluez.Device1") == 0 && g_variant_lookup(values, "Connected", "b", &state))
            {
                if (state) self.connected_devices.insert(path);
                else
                {
                    self.connected_devices.erase(path);
                    if (self.agent) self.agent->clear(path);
                    if (self.peer == path) self.disconnected();
                }
            }
            if (self.adapter == path && std::strcmp(interface, "org.bluez.Adapter1") == 0 && g_variant_lookup(values, "Powered", "b", &state) && !state)
                self.unavailable("Bluetooth adapter powered off");
            g_variant_unref(values);
            g_variant_unref(invalidated);
        }
    }
    static GVariant* property(GDBusConnection*, const gchar*, const gchar* path, const gchar* interface, const gchar* name, GError** error, gpointer opaque)
    {
        auto& self = *static_cast<Impl*>(opaque);
        auto* values = g_variant_ref_sink(self.properties(path, interface));
        auto* value = g_variant_lookup_value(values, name, nullptr);
        g_variant_unref(values);
        if (!value) g_set_error_literal(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property");
        return value;
    }
    static void method(GDBusConnection*, const gchar* sender, const gchar* path, const gchar* interface, const gchar* method,
                       GVariant* parameters, GDBusMethodInvocation* invocation, gpointer opaque)
    {
        auto& self = *static_cast<Impl*>(opaque);
        try
        {
            self.dispatch(sender, path, interface, method, parameters, invocation);
        }
        catch (...)
        {
            g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Failed", "Phone transport callback failed");
        }
    }
    void dispatch(const char* sender, const char* path, const char* interface, const char* method,
                  GVariant* parameters, GDBusMethodInvocation* invocation)
    {
        auto fail = [&](const char* error, const char* message)
        { g_dbus_method_invocation_return_dbus_error(invocation, error, message); };
        if (std::strcmp(interface, object_interface) == 0)
        {
            g_dbus_method_invocation_return_value(invocation, managedObjects());
            return;
        }
        if (!sender || owner != sender)
        {
            fail("org.bluez.Error.NotAuthorized", "Only the registered BlueZ owner may access phone operations");
            return;
        }
        if (std::strcmp(interface, advert_interface) == 0)
        {
            unavailable("Bluetooth advertisement released");
            g_dbus_method_invocation_return_value(invocation, nullptr);
            return;
        }
        auto* item = find(path);
        if (!active || !item)
        {
            fail("org.bluez.Error.Failed", "Phone transport unavailable");
            return;
        }
        if (std::strcmp(method, "StartNotify") == 0 || std::strcmp(method, "StopNotify") == 0)
        {
            if (!item->notifiable)
            {
                fail("org.bluez.Error.NotSupported", "Characteristic has no notifications");
                return;
            }
            const bool enabled = std::strcmp(method, "StartNotify") == 0;
            if (item->subscribed != enabled)
            {
                item->subscribed = enabled;
                try
                {
                    if (hooks.subscription) hooks.subscription(item->channel, enabled);
                }
                catch (...)
                {
                    item->subscribed = !enabled;
                    throw;
                }
                changed(*item, "Notifying", g_variant_new_boolean(enabled));
            }
            g_dbus_method_invocation_return_value(invocation, nullptr);
            return;
        }
        const bool reading = std::strcmp(method, "ReadValue") == 0;
        if ((reading && !item->readable) || (!reading && !item->writable))
        {
            fail("org.bluez.Error.NotPermitted", "Characteristic does not support this operation");
            return;
        }
        GVariant* values = g_variant_get_child_value(parameters, reading ? 0 : 1);
        const char* device = nullptr;
        const char* type = nullptr;
        const char* link = nullptr;
        guint16 offset = 0, requested_mtu = mtu;
        gboolean prepared = false;
        g_variant_lookup(values, "device", "&o", &device);
        g_variant_lookup(values, "offset", "q", &offset);
        g_variant_lookup(values, "mtu", "q", &requested_mtu);
        g_variant_lookup(values, "prepare-authorize", "b", &prepared);
        g_variant_lookup(values, "type", "&s", &type);
        g_variant_lookup(values, "link", "&s", &link);
        const std::string requested_peer = device ? device : "";
        const bool valid_peer = requested_peer.compare(0, adapter.size() + 5, adapter + "/dev_") == 0;
        const bool valid_type = !type || std::strcmp(type, "request") == 0 || std::strcmp(type, "command") == 0;
        const bool valid_link = !link || std::strcmp(link, "LE") == 0;
        g_variant_unref(values);
        if (!valid_peer || !valid_link || requested_mtu < 23 || requested_mtu > 517)
        {
            fail("org.bluez.Error.Failed", "Invalid peer or ATT parameters");
            return;
        }
        if (!peer.empty() && peer != requested_peer)
        {
            fail("org.bluez.Error.InProgress", "Another phone owns this session");
            return;
        }
        if (!reading && (offset != 0 || prepared || !valid_type))
        {
            fail("org.bluez.Error.InvalidOffset", "Only complete writes are supported");
            return;
        }
        if (peer.empty())
        {
            if (hooks.connected) hooks.connected(requested_peer);
            peer = requested_peer;
        }
        mtu = requested_mtu;
        connected_devices.insert(peer);
        if (reading)
        {
            std::vector<uint8_t> bytes;
            auto result = item->framing.read(
                peer, mtu, offset, [&](auto& output)
                { return hooks.read && hooks.read(item->channel, output); },
                bytes);
            if (result != PhoneGattSession::Result::Ok)
            {
                fail(resultError(result), "Phone read failed");
                return;
            }
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ay)", byteArray(bytes)));
        }
        else
        {
            auto* data = g_variant_get_child_value(parameters, 0);
            gsize length = 0;
            const auto* begin = static_cast<const uint8_t*>(g_variant_get_fixed_array(data, &length, 1));
            std::vector<uint8_t> bytes;
            if (length <= 512 && length != 0) bytes.assign(begin, begin + length);
            g_variant_unref(data);
            auto result = item->framing.write(peer, offset, false, bytes, [&](const auto& input)
                                              { return hooks.write && hooks.write(item->channel, input); });
            if (result != PhoneGattSession::Result::Ok)
            {
                fail(resultError(result), "Phone write rejected");
                return;
            }
            g_dbus_method_invocation_return_value(invocation, nullptr);
        }
    }
    bool exportObject(const char* path, const char* interface)
    {
        static const GDBusInterfaceVTable vtable = {method, property, nullptr, {}};
        GError* error = nullptr;
        auto id = g_dbus_connection_register_object(connection, path, g_dbus_node_info_lookup_interface(description, interface), &vtable, this, nullptr, &error);
        if (!id) failure = error ? error->message : "Cannot export GATT object";
        g_clear_error(&error);
        if (id) registrations.push_back(id);
        return id != 0;
    }
    bool start(const BluezPhoneGattOptions& value, BluezPhoneGattHooks callbacks)
    {
        options = value;
        hooks = std::move(callbacks);
        if (static_cast<unsigned>(options.pairing_mode) > static_cast<unsigned>(PhonePairingMode::FixedPin))
        {
            failure = "Invalid Bluetooth pairing policy";
            return false;
        }
        if (options.local_name.empty() || options.local_name.size() > 26 || !g_utf8_validate(options.local_name.c_str(), options.local_name.size(), nullptr) || options.local_name.find('\0') != std::string::npos)
        {
            failure = "Invalid Bluetooth local name";
            return false;
        }
        const auto selection = selectBluezAdapter(queryBluezAdapters(options.bus_address), options.adapter_address);
        if (selection.status != BluezSelectionStatus::Ready)
        {
            failure = "No unique powered Bluetooth adapter with GATT and advertising support";
            return false;
        }
        adapter = selection.adapter.path;
        context = g_main_context_new();
        ContextScope scope(context);
        connection = detail::connectBluezBus(options.bus_address, failure);
        if (!connection) return false;
        GError* error = nullptr;
        auto* reply = g_dbus_connection_call_sync(connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner",
                                                  g_variant_new("(s)", "org.bluez"), G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error);
        if (!reply)
        {
            failure = error ? error->message : "BlueZ owner unavailable";
            g_clear_error(&error);
            return false;
        }
        const char* name;
        g_variant_get(reply, "(&s)", &name);
        owner = name;
        g_variant_unref(reply);
        description = g_dbus_node_info_new_for_xml(xml, &error);
        if (!description)
        {
            failure = error ? error->message : "Invalid GATT schema";
            g_clear_error(&error);
            return false;
        }
        if (options.protocol == PhoneGattProtocol::Meshtastic)
        {
            characteristics.emplace_back(PhoneGattChannel::ToRadio, TORADIO_UUID, false, true, false, false, 0, 504);
            characteristics.emplace_back(PhoneGattChannel::FromRadio, FROMRADIO_UUID, true, false, false, false, 510, 0);
            characteristics.emplace_back(PhoneGattChannel::FromNum, FROMNUM_UUID, true, false, true, false, 4, 0);
            characteristics.emplace_back(PhoneGattChannel::LogRadio, LOGRADIO_UUID, true, false, true, false, 510, 0);
            if (options.enable_from_radio_sync)
                characteristics.emplace_back(PhoneGattChannel::FromRadioSync, FROMRADIOSYNC_UUID, false, false, true, true, 510, 0);
        }
        else
        {
            characteristics.emplace_back(PhoneGattChannel::MeshCoreRx, NUS_CHAR_RX_UUID, false, true, false, false, 0, 172);
            characteristics.emplace_back(PhoneGattChannel::MeshCoreTx, NUS_CHAR_TX_UUID, false, false, true, false, 172, 0);
        }
        if (!exportObject(root, object_interface) || !exportObject(service_path, service_interface) || !exportObject(advert_path, advert_interface)) return false;
        for (size_t i = 0; i < characteristics.size(); ++i)
        {
            characteristics[i].path = std::string(service_path) + "/char" + std::to_string(i);
            if (!exportObject(characteristics[i].path.c_str(), char_interface)) return false;
        }
        device_subscription = g_dbus_connection_signal_subscribe(connection, owner.c_str(), properties_interface, "PropertiesChanged", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, signal, this, nullptr);
        removed_subscription = g_dbus_connection_signal_subscribe(connection, owner.c_str(), object_interface, "InterfacesRemoved", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, signal, this, nullptr);
        added_subscription = g_dbus_connection_signal_subscribe(connection, owner.c_str(), object_interface, "InterfacesAdded", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, signal, this, nullptr);
        owner_subscription = g_dbus_connection_signal_subscribe(connection, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus", "org.bluez", G_DBUS_SIGNAL_FLAGS_NONE, signal, this, nullptr);
        // Include devices connected before this application started. Subscribe first
        // so a concurrent disconnect is applied when the registration loop pumps.
        auto* objects_reply = g_dbus_connection_call_sync(connection, owner.c_str(), "/", object_interface, "GetManagedObjects", nullptr,
                                                          G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error);
        if (!objects_reply)
        {
            failure = error ? error->message : "Cannot inspect existing Bluetooth connections";
            g_clear_error(&error);
            return false;
        }
        auto* objects = g_variant_get_child_value(objects_reply, 0);
        GVariantIter iterator;
        g_variant_iter_init(&iterator, objects);
        const char* path;
        GVariant* interfaces;
        while (g_variant_iter_next(&iterator, "{&o@a{sa{sv}}}", &path, &interfaces))
        {
            if (std::string(path).compare(0, adapter.size() + 5, adapter + "/dev_") == 0)
            {
                if (auto* values = g_variant_lookup_value(interfaces, "org.bluez.Device1", G_VARIANT_TYPE_VARDICT))
                {
                    gboolean connected = false;
                    if (g_variant_lookup(values, "Connected", "b", &connected) && connected) connected_devices.insert(path);
                    g_variant_unref(values);
                }
            }
            g_variant_unref(interfaces);
        }
        g_variant_unref(objects);
        g_variant_unref(objects_reply);
        if (options.pairing_mode == PhonePairingMode::RandomPin || options.pairing_mode == PhonePairingMode::FixedPin)
        {
            agent = std::make_unique<detail::BluezPairingAgent>(connection, context, owner, adapter, serviceUuid(), options, hooks.pairing,
                                                                [this](const auto& requested)
                                                                { return peer.empty() || peer == requested; });
            if (!agent->start())
            {
                failure = agent->error();
                return false;
            }
        }
        if (!call("org.bluez.GattManager1", "RegisterApplication", root)) return false;
        if (!failure.empty()) return false;
        if (!call("org.bluez.LEAdvertisingManager1", "RegisterAdvertisement", advert_path)) return false;
        if (!failure.empty()) return false;
        active = true;
        return true;
    }
    void stop()
    {
        active = false;
        agent.reset();
        disconnected();
        if (connection)
        {
            if (device_subscription) g_dbus_connection_signal_unsubscribe(connection, device_subscription);
            if (removed_subscription) g_dbus_connection_signal_unsubscribe(connection, removed_subscription);
            if (added_subscription) g_dbus_connection_signal_unsubscribe(connection, added_subscription);
            if (owner_subscription) g_dbus_connection_signal_unsubscribe(connection, owner_subscription);
            for (auto id : registrations) g_dbus_connection_unregister_object(connection, id);
            // A dedicated bus connection makes close unregister all BlueZ resources.
            g_dbus_connection_close_sync(connection, nullptr, nullptr);
            g_clear_object(&connection);
        }
        device_subscription = removed_subscription = added_subscription = owner_subscription = 0;
        registrations.clear();
        if (description) g_dbus_node_info_unref(description);
        description = nullptr;
        if (context) g_main_context_unref(context);
        context = nullptr;
        characteristics.clear();
        connected_devices.clear();
        hooks = {};
        owner.clear();
        adapter.clear();
    }
#endif
};

BluezPhoneGatt::BluezPhoneGatt() : impl_(std::make_unique<Impl>()) {}
BluezPhoneGatt::~BluezPhoneGatt() { stop(); }
bool BluezPhoneGatt::start(const BluezPhoneGattOptions& options, BluezPhoneGattHooks hooks)
{
    stop();
    impl_->failure.clear();
#if defined(TRAIL_MATE_BLUEZ_GIO)
    if (impl_->start(options, std::move(hooks))) return true;
    impl_->stop();
#else
    (void)options;
    (void)hooks;
    impl_->failure = "GIO D-Bus support was not built";
#endif
    return false;
}
void BluezPhoneGatt::stop()
{
#if defined(TRAIL_MATE_BLUEZ_GIO)
    impl_->stop();
#endif
}
void BluezPhoneGatt::poll()
{
#if defined(TRAIL_MATE_BLUEZ_GIO)
    if (!impl_->context) return;
    ContextScope scope(impl_->context);
    // Bound each pump so protocol/radio work cannot starve under a busy bus.
    for (size_t i = 0; i < 64 && g_main_context_pending(impl_->context); ++i)
        g_main_context_iteration(impl_->context, false);
    if (impl_->agent)
    {
        impl_->agent->poll();
        if (impl_->active && !impl_->agent->ready()) impl_->unavailable("Bluetooth pairing agent released");
    }
    if (impl_->connection && g_dbus_connection_is_closed(impl_->connection) && impl_->active)
        impl_->unavailable("Bluetooth bus disconnected");
#endif
}
bool BluezPhoneGatt::ready() const { return impl_->active; }
const std::string& BluezPhoneGatt::error() const { return impl_->failure; }
bool BluezPhoneGatt::notify(PhoneGattChannel channel, const std::vector<uint8_t>& bytes)
{
#if defined(TRAIL_MATE_BLUEZ_GIO)
    if (!impl_->active) return false;
    if (bytes.empty()) return false;
    if (channel == PhoneGattChannel::FromNum && bytes.size() != 4) return false;
    for (auto& item : impl_->characteristics)
    {
        if (item.channel != channel || !item.subscribed) continue;
        const bool counter = channel == PhoneGattChannel::FromNum && bytes.size() == 4;
        if (!counter && (impl_->peer.empty() || impl_->connected_devices.size() != 1 || !impl_->connected_devices.count(impl_->peer) ||
                         bytes.size() > size_t(impl_->mtu - 3) || bytes.size() > 510)) return false;
        if (channel == PhoneGattChannel::MeshCoreTx && bytes.size() > 172) return false;
        return impl_->changed(item, "Value", byteArray(bytes));
    }
#else
    (void)channel;
    (void)bytes;
#endif
    return false;
}
bool BluezPhoneGatt::disconnectPhone()
{
#if defined(TRAIL_MATE_BLUEZ_GIO)
    if (!impl_->active || impl_->peer.empty()) return false;
    ContextScope scope(impl_->context);
    Reply reply;
    const auto peer = impl_->peer;
    g_dbus_connection_call(impl_->connection, impl_->owner.c_str(), peer.c_str(), "org.bluez.Device1", "Disconnect", nullptr,
                           G_VARIANT_TYPE_UNIT, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, replied, &reply);
    while (!reply.done) g_main_context_iteration(impl_->context, true);
    const bool success = reply.value != nullptr;
    if (reply.value) g_variant_unref(reply.value);
    g_clear_error(&reply.error);
    if (success)
    {
        impl_->connected_devices.erase(peer);
        if (impl_->peer == peer) impl_->disconnected();
    }
    return success;
#else
    return false;
#endif
}
} // namespace platform::linux_runtime
