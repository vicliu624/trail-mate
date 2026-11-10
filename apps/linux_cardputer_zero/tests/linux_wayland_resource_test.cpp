#include "platform/wayland/wayland_presenter.h"
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <wayland-server.h>

namespace
{
std::size_t descriptorCount()
{
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd"))
    {
        (void)entry;
        ++count;
    }
    return count;
}

void rejectIncompleteCompositor()
{
    auto* server = wl_display_create();
    assert(server != nullptr);
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0);
    assert(wl_client_create(server, sockets[0]) != nullptr);
    const auto socket = std::to_string(sockets[1]);
    assert(setenv("WAYLAND_SOCKET", socket.c_str(), 1) == 0);
    std::atomic_bool stopping{false};
    std::thread worker([&]
                       {
        auto* loop = wl_display_get_event_loop(server);
        while (!stopping)
        {
            assert(wl_event_loop_dispatch(loop, 10) >= 0);
            wl_display_flush_clients(server);
        } });
    bool rejected = false;
    try
    {
        trailmate::cardputer_zero::platform::wayland::WaylandPresenter presenter;
    }
    catch (const std::runtime_error& error)
    {
        rejected = std::string(error.what()).find("missing wl_compositor") != std::string::npos;
    }
    stopping = true;
    worker.join();
    wl_display_destroy_clients(server);
    wl_display_destroy(server);
    assert(rejected);
}
} // namespace

int main()
{
    const auto baseline = descriptorCount();
    for (unsigned attempt = 0; attempt < 20; ++attempt)
    {
        rejectIncompleteCompositor();
        assert(descriptorCount() == baseline);
    }
}
