#include "bluez_bus.h"
#if defined(TRAIL_MATE_BLUEZ_GIO)
namespace platform::linux_runtime::detail
{
namespace
{
struct Connect
{
    GDBusConnection* connection = nullptr;
    GError* error = nullptr;
    bool done = false;
};
void connected(GObject*, GAsyncResult* result, gpointer opaque)
{
    auto& state = *static_cast<Connect*>(opaque);
    state.connection = g_dbus_connection_new_for_address_finish(result, &state.error);
    state.done = true;
}
gboolean cancelConnect(gpointer opaque)
{
    g_cancellable_cancel(static_cast<GCancellable*>(opaque));
    return G_SOURCE_REMOVE;
}
}
GDBusConnection* connectBluezBus(const std::string& requested, std::string& error)
{
    Connect state;
    gchar* address = requested.empty() ? g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SYSTEM, nullptr, &state.error) : g_strdup(requested.c_str());
    if (!address)
    {
        error = state.error ? state.error->message : "D-Bus address unavailable";
        g_clear_error(&state.error);
        return nullptr;
    }
    auto* context = g_main_context_new();
    auto* cancel = g_cancellable_new();
    auto* deadline = g_timeout_source_new(2000);
    g_main_context_push_thread_default(context);
    g_source_set_callback(deadline, cancelConnect, cancel, nullptr);
    g_source_attach(deadline, context);
    g_dbus_connection_new_for_address(address, static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
                                      nullptr, cancel, connected, &state);
    while (!state.done) g_main_context_iteration(context, true);
    g_source_destroy(deadline);
    g_source_unref(deadline);
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    g_object_unref(cancel);
    g_free(address);
    if (!state.connection) error = state.error ? state.error->message : "D-Bus connection unavailable";
    g_clear_error(&state.error);
    return state.connection;
}
}
#endif
