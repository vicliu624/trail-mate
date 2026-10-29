#include "platform/linux/network_status.h"
#include <arpa/inet.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <ifaddrs.h>
#include <mutex>
#include <net/if.h>
#include <thread>

namespace platform::linux_runtime
{
namespace
{
bool usable_v4(const in_addr& address)
{
    const auto host = ntohl(address.s_addr);
    return (host >> 24) != 0 && (host >> 24) != 127 && (host >> 24) < 224 &&
           (host >> 16) != 0xA9FE;
}
bool usable_address(const std::string& text)
{
    in_addr v4{};
    if (inet_pton(AF_INET, text.c_str(), &v4) == 1) return usable_v4(v4);
    in6_addr v6{};
    if (inet_pton(AF_INET6, text.c_str(), &v6) != 1 || IN6_IS_ADDR_UNSPECIFIED(&v6) ||
        IN6_IS_ADDR_LOOPBACK(&v6) || IN6_IS_ADDR_LINKLOCAL(&v6) || IN6_IS_ADDR_MULTICAST(&v6)) return false;
    if (IN6_IS_ADDR_V4MAPPED(&v6))
    {
        std::memcpy(&v4, v6.s6_addr + 12, sizeof(v4));
        return usable_v4(v4);
    }
    return true;
}
} // namespace
platform::ui::network::Status classify_network_addresses(const std::vector<NetworkAddress>& addresses)
{
    platform::ui::network::Status result{true, false};
    for (const auto& address : addresses)
        if (!address.interface.empty() && address.up && address.running && !address.loopback && usable_address(address.address))
            result.connected = true;
    return result;
}
platform::ui::network::Status read_network_status()
{
    ifaddrs* addresses = nullptr;
    if (getifaddrs(&addresses) != 0) return {};
    std::vector<NetworkAddress> links;
    for (auto* item = addresses; item; item = item->ifa_next)
    {
        if (!item->ifa_addr || !item->ifa_name) continue;
        const auto family = item->ifa_addr->sa_family;
        const void* address = nullptr;
        if (family == AF_INET) address = &reinterpret_cast<sockaddr_in*>(item->ifa_addr)->sin_addr;
        else if (family == AF_INET6) address = &reinterpret_cast<sockaddr_in6*>(item->ifa_addr)->sin6_addr;
        else continue;
        char text[INET6_ADDRSTRLEN]{};
        if (!inet_ntop(family, address, text, sizeof(text))) continue;
        links.push_back({item->ifa_name, text, bool(item->ifa_flags & IFF_UP), bool(item->ifa_flags & IFF_RUNNING), bool(item->ifa_flags & IFF_LOOPBACK)});
    }
    freeifaddrs(addresses);
    return classify_network_addresses(links);
}
} // namespace platform::linux_runtime

namespace platform::ui::network
{
namespace
{
class Monitor
{
  public:
    Monitor() : worker_([this] { run(); }) {}
    ~Monitor()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        worker_.join();
    }
    Status snapshot()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }
  private:
    void run()
    {
        for (;;)
        {
            const auto next = platform::linux_runtime::read_network_status();
            std::unique_lock<std::mutex> lock(mutex_);
            status_ = next;
            if (wake_.wait_for(lock, std::chrono::seconds(1), [&] { return stopping_; })) break;
        }
    }
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
    Status status_{};
    std::thread worker_;
};
} // namespace
Status status()
{
    static Monitor monitor;
    return monitor.snapshot();
}
} // namespace platform::ui::network
