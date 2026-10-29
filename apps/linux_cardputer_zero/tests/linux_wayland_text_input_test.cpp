#include "platform/wayland/wayland_text_input.h"
#include "text-input-unstable-v3-server-protocol.h"
#include <cassert>
#include <cstring>
#include <memory>
#include <poll.h>
#include <sys/socket.h>
#include <wayland-server.h>

using namespace trailmate::cardputer_zero;
namespace wayland = trailmate::cardputer_zero::platform::wayland;
struct Wire
{
    wl_display* server = wl_display_create();
    wl_display* client = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_seat* seat = nullptr;
    zwp_text_input_manager_v3* manager = nullptr;
    wl_surface* surface = nullptr;
    wl_resource* input = nullptr;
    wl_resource* server_surface = nullptr;
    std::vector<app::InputEvent> events;
    std::unique_ptr<wayland::WaylandTextInput> adapter;
    uint32_t serial = 0;
    bool enabled = false;
    std::string surrounding;
    uint32_t purpose = 0;
    static Wire& self(wl_resource* resource) { return *static_cast<Wire*>(wl_resource_get_user_data(resource)); }
    static void destroy(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
    inline static const struct wl_surface_interface surface_impl
    {
        destroy
    };
    static void createSurface(wl_client* client, wl_resource* resource, uint32_t id)
    {
        auto& wire = self(resource);
        wire.server_surface = wl_resource_create(client, &wl_surface_interface, 1, id);
        wl_resource_set_implementation(wire.server_surface, &surface_impl, &wire, nullptr);
    }
    inline static const struct wl_compositor_interface compositor_impl
    {
        createSurface, nullptr
    };
    static void enable(wl_client*, wl_resource* resource) { self(resource).enabled = true; }
    static void disable(wl_client*, wl_resource* resource) { self(resource).enabled = false; }
    static void text(wl_client*, wl_resource* resource, const char* text, int32_t cursor, int32_t anchor)
    {
        assert(cursor == anchor && cursor >= 0 && static_cast<size_t>(cursor) <= std::strlen(text));
        self(resource).surrounding = text;
    }
    static void cause(wl_client*, wl_resource*, uint32_t) {}
    static void content(wl_client*, wl_resource* resource, uint32_t, uint32_t purpose) { self(resource).purpose = purpose; }
    static void rect(wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {}
    static void commit(wl_client*, wl_resource* resource) { ++self(resource).serial; }
    inline static const struct zwp_text_input_v3_interface input_impl
    {
        destroy, enable, disable, text, cause, content, rect, commit
    };
    static void getInput(wl_client* client, wl_resource* resource, uint32_t id, wl_resource*)
    {
        auto& wire = self(resource);
        wire.input = wl_resource_create(client, &zwp_text_input_v3_interface, 1, id);
        wl_resource_set_implementation(wire.input, &input_impl, &wire, nullptr);
    }
    inline static const struct zwp_text_input_manager_v3_interface manager_impl
    {
        destroy, getInput
    };
    static void bindManager(wl_client* client, void* data, uint32_t, uint32_t id)
    {
        auto* resource = wl_resource_create(client, &zwp_text_input_manager_v3_interface, 1, id);
        wl_resource_set_implementation(resource, &manager_impl, data, nullptr);
    }
    static void bindSeat(wl_client* client, void* data, uint32_t, uint32_t id)
    {
        auto* resource = wl_resource_create(client, &wl_seat_interface, 1, id);
        wl_resource_set_implementation(resource, nullptr, data, nullptr);
    }
    static void bindCompositor(wl_client* client, void* data, uint32_t, uint32_t id)
    {
        auto* resource = wl_resource_create(client, &wl_compositor_interface, 1, id);
        wl_resource_set_implementation(resource, &compositor_impl, data, nullptr);
    }
    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t)
    {
        auto& wire = *static_cast<Wire*>(data);
        if (!std::strcmp(interface, "wl_seat")) wire.seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
        if (!std::strcmp(interface, "wl_compositor")) wire.compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 1));
        if (!std::strcmp(interface, "zwp_text_input_manager_v3")) wire.manager = static_cast<zwp_text_input_manager_v3*>(wl_registry_bind(registry, name, &zwp_text_input_manager_v3_interface, 1));
    }
    static void removed(void*, wl_registry*, uint32_t) {}
    inline static const wl_registry_listener registry_listener{global, removed};
    void cycle()
    {
        assert(wl_display_flush(client) >= 0);
        assert(wl_event_loop_dispatch(wl_display_get_event_loop(server), 0) >= 0);
        wl_display_flush_clients(server);
        pollfd fd{wl_display_get_fd(client), POLLIN, 0};
        if (poll(&fd, 1, 0) > 0) assert(wl_display_dispatch(client) >= 0);
        assert(wl_display_dispatch_pending(client) >= 0);
    }
    void pump()
    {
        for (int i = 0; i < 8; ++i) cycle();
    }
    Wire()
    {
        wl_global_create(server, &wl_compositor_interface, 1, this, bindCompositor);
        wl_global_create(server, &wl_seat_interface, 1, this, bindSeat);
        wl_global_create(server, &zwp_text_input_manager_v3_interface, 1, this, bindManager);
        int pair[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
        assert(wl_client_create(server, pair[0]));
        client = wl_display_connect_to_fd(pair[1]);
        assert(client);
        registry = wl_display_get_registry(client);
        wl_registry_add_listener(registry, &registry_listener, this);
        pump();
        assert(manager && seat && compositor);
        surface = wl_compositor_create_surface(compositor);
        adapter = std::make_unique<wayland::WaylandTextInput>(manager, seat, surface, events);
        pump();
        assert(input && server_surface);
        zwp_text_input_v3_send_enter(input, server_surface);
        pump();
    }
    ~Wire()
    {
        adapter.reset();
        wl_surface_destroy(surface);
        zwp_text_input_manager_v3_destroy(manager);
        wl_seat_destroy(seat);
        wl_compositor_destroy(compositor);
        wl_registry_destroy(registry);
        wl_display_disconnect(client);
        wl_display_destroy_clients(server);
        wl_display_destroy(server);
    }
};
int main()
{
    Wire wire;
    app::TextInputState state;
    state.enabled = true;
    state.focus = 10;
    state.surrounding = "A\xe4\xb8\xad";
    state.cursor = 4;
    wire.adapter->update(state);
    wire.pump();
    assert(wire.enabled && wire.serial == 1 && wire.surrounding == state.surrounding);
    zwp_text_input_v3_send_commit_string(wire.input, "\xe6\x96\x87\xf0\x9f\x99\x82");
    zwp_text_input_v3_send_delete_surrounding_text(wire.input, 3, 0);
    zwp_text_input_v3_send_preedit_string(wire.input, "\xe6\x8b\xbc", 0, 3);
    wire.pump();
    assert(wire.events.empty()); // Protocol events remain pending until done.
    zwp_text_input_v3_send_done(wire.input, wire.serial);
    wire.pump();
    assert(wire.events.size() == 1 && wire.events[0].edit.focus == 10 && wire.events[0].edit.before == 3);
    assert(wire.events[0].edit.commit == "\xe6\x96\x87\xf0\x9f\x99\x82");
    wire.events.clear();
    const auto old_serial = wire.serial;
    state.focus = 11;
    wire.adapter->update(state);
    wire.pump();
    assert(wire.serial == old_serial + 2); // Disable old editor before enabling next.
    zwp_text_input_v3_send_commit_string(wire.input, "stale");
    zwp_text_input_v3_send_done(wire.input, old_serial);
    wire.pump();
    assert(wire.events.empty());
    zwp_text_input_v3_send_commit_string(wire.input, "\xc0\xaf");
    zwp_text_input_v3_send_done(wire.input, wire.serial);
    wire.pump();
    assert(wire.events.empty());
    // A stale acknowledgement for this same editor still delivers text, but
    // surrounding-state updates wait for the outstanding matching serial.
    state.surrounding = "new text";
    state.cursor = 8;
    wire.adapter->update(state);
    wire.pump();
    const auto latest = wire.serial;
    zwp_text_input_v3_send_commit_string(wire.input, "late same editor");
    zwp_text_input_v3_send_done(wire.input, latest - 1);
    wire.pump();
    assert(wire.events.size() == 1 && wire.events[0].edit.focus == state.focus);
    wire.events.clear();
    state.surrounding = "deferred text";
    state.cursor = 13;
    wire.adapter->update(state);
    wire.pump();
    assert(wire.serial == latest);
    zwp_text_input_v3_send_done(wire.input, latest);
    wire.pump();
    wire.events.clear();
    wire.adapter->update(state);
    wire.pump();
    assert(wire.serial == latest + 1 && wire.surrounding == "deferred text");
    state.focus = 12;
    state.password = true;
    state.surrounding.clear();
    state.cursor = 0;
    wire.adapter->update(state);
    wire.pump();
    assert(wire.purpose == ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD);
    state.enabled = false;
    wire.adapter->update(state);
    wire.pump();
    assert(!wire.enabled);
    zwp_text_input_v3_send_commit_string(wire.input, "disabled");
    zwp_text_input_v3_send_done(wire.input, wire.serial);
    wire.pump();
    assert(wire.events.empty());
}
