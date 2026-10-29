#include "platform/linux/network_status.h"
#include <cassert>

int main()
{
    using namespace platform::linux_runtime;
    assert(classify_network_addresses({}).supported);
    assert(!classify_network_addresses({}).connected);
    const NetworkAddress wireless{"wl-renamed", "192.0.2.10", true, true, false};
    const NetworkAddress wired{"usb-renamed", "198.51.100.20", true, true, false};
    assert(classify_network_addresses({wireless}).connected);
    assert(classify_network_addresses({wired}).connected);
    assert(classify_network_addresses({{"other", "fd12::2", true, true, false}}).connected);
    for (const auto* address : {"0.0.0.0", "127.0.0.1", "169.254.2.3", "224.0.0.1", "::", "::1", "fe80::1", "ff02::1", "::ffff:127.0.0.1", "bad"})
        assert(!classify_network_addresses({{"arbitrary", address, true, true, false}}).connected);
    auto down = wired;
    down.running = false;
    assert(!classify_network_addresses({down}).connected);
    assert(classify_network_addresses({down, wireless}).connected);
    down = wired;
    down.up = false;
    assert(!classify_network_addresses({down}).connected);
    down = wired;
    down.loopback = true;
    assert(!classify_network_addresses({down}).connected);
}
