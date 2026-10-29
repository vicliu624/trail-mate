#include "platform/linux/bluez_adapters.h"
#include "bluez_bus.h"
#include <cctype>
#if defined(TRAIL_MATE_BLUEZ_GIO)
#include <gio/gio.h>
#endif

namespace platform::linux_runtime
{
namespace
{
std::string normalizedAddress(const std::string& address)
{
    if (address.size() != 17) return {};
    std::string result = address;
    for (size_t i = 0; i < result.size(); ++i)
    {
        const auto value = static_cast<unsigned char>(result[i]);
        if (i % 3 == 2)
        {
            if (value != ':') return {};
        }
        else
        {
            if (!std::isxdigit(value)) return {};
            result[i] = static_cast<char>(std::toupper(value));
        }
    }
    return result;
}
} // namespace
BluezSelection selectBluezAdapter(const BluezAdapters& listing, const std::string& address)
{
    BluezSelection result;
    if (!listing.queried) return result;
    const auto requested = normalizedAddress(address);
    if (!address.empty() && requested.empty())
    {
        result.status = BluezSelectionStatus::InvalidAddress;
        return result;
    }
    size_t matches = 0;
    for (const auto& adapter : listing.adapters)
    {
        const auto actual = normalizedAddress(adapter.address);
        if (actual.empty() || (!address.empty() && requested != actual)) continue;
        if (address.empty() && (!adapter.gatt || !adapter.advertising)) continue;
        result.adapter = adapter;
        ++matches;
    }
    if (!matches)
        result.status = address.empty() && !listing.adapters.empty() ? BluezSelectionStatus::Unsupported : BluezSelectionStatus::Missing;
    else if (matches > 1)
    {
        result.adapter = {};
        result.status = BluezSelectionStatus::Ambiguous;
    }
    else if (!result.adapter.gatt || !result.adapter.advertising) result.status = BluezSelectionStatus::Unsupported;
    else result.status = result.adapter.powered ? BluezSelectionStatus::Ready : BluezSelectionStatus::PoweredOff;
    return result;
}
BluezAdapters queryBluezAdapters(const std::string& bus_address)
{
    BluezAdapters result;
#if defined(TRAIL_MATE_BLUEZ_GIO)
    auto* connection = detail::connectBluezBus(bus_address, result.error);
    if (!connection) return result;
    GError* error = nullptr;
    auto* reply = g_dbus_connection_call_sync(connection, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", nullptr,
                                              G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error);
    g_object_unref(connection);
    if (!reply)
    {
        result.error = error ? error->message : "BlueZ object manager unavailable";
        g_clear_error(&error);
        return result;
    }
    GVariant* objects = g_variant_get_child_value(reply, 0);
    GVariantIter iterator;
    g_variant_iter_init(&iterator, objects);
    const char* path;
    GVariant* interfaces;
    while (g_variant_iter_next(&iterator, "{&o@a{sa{sv}}}", &path, &interfaces))
    {
        if (auto* properties = g_variant_lookup_value(interfaces, "org.bluez.Adapter1", G_VARIANT_TYPE_VARDICT))
        {
            const char* address = nullptr;
            if (g_variant_lookup(properties, "Address", "&s", &address) && !normalizedAddress(address).empty())
            {
                BluezAdapter adapter;
                adapter.path = path;
                adapter.address = normalizedAddress(address);
                const char* name = nullptr;
                if (g_variant_lookup(properties, "Alias", "&s", &name) || g_variant_lookup(properties, "Name", "&s", &name)) adapter.name = name;
                gboolean powered = false;
                g_variant_lookup(properties, "Powered", "b", &powered);
                adapter.powered = powered;
                auto* gatt = g_variant_lookup_value(interfaces, "org.bluez.GattManager1", G_VARIANT_TYPE_VARDICT);
                auto* advertising = g_variant_lookup_value(interfaces, "org.bluez.LEAdvertisingManager1", G_VARIANT_TYPE_VARDICT);
                adapter.gatt = gatt != nullptr;
                adapter.advertising = advertising != nullptr;
                if (gatt) g_variant_unref(gatt);
                if (advertising) g_variant_unref(advertising);
                result.adapters.push_back(std::move(adapter));
            }
            g_variant_unref(properties);
        }
        g_variant_unref(interfaces);
    }
    result.queried = true;
    g_variant_unref(objects);
    g_variant_unref(reply);
#else
    (void)bus_address;
    result.error = "GIO D-Bus support was not built";
#endif
    return result;
}
} // namespace platform::linux_runtime
