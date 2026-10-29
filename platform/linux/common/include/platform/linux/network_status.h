#pragma once
#include "platform/ui/network_runtime.h"
#include <string>
#include <vector>

namespace platform::linux_runtime
{
struct NetworkAddress
{
    std::string interface;
    std::string address;
    bool up = false;
    bool running = false;
    bool loopback = false;
};
platform::ui::network::Status classify_network_addresses(const std::vector<NetworkAddress>& addresses);
platform::ui::network::Status read_network_status();
} // namespace platform::linux_runtime
