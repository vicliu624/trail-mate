#include "chat/linux_reticulum_auto.h"
#include "chat/domain/reticulum_network_validation.h"
#include "chat/infra/reticulum/interface_access_codec.h"
#include <SHA256.h>
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace trailmate::linux_app
{
namespace r = chat::reticulum;
namespace
{
constexpr uint32_t kAnnounceMs = 1600, kReverseMs = 5200, kPeerTimeoutMs = 22000, kScanMs = 4000, kDuplicateMs = 750;
constexpr size_t kWireBytes = r::kReticulumMtu + r::InterfaceAccessCodec::kMaxTagSize;
using Address = LinuxReticulumAuto::Address;
bool groupValid(const char* group) { return group && group[0] && std::memchr(group, 0, r::kAutoInterfaceGroupMaxLen + 1); }
void digest(const uint8_t* bytes, size_t size, uint8_t* output)
{
    SHA256 hash;
    hash.update(bytes, size);
    hash.finalize(output, 32);
}
sockaddr_in6 endpoint(const Address& address, uint16_t port, uint32_t scope)
{
    sockaddr_in6 result{};
    result.sin6_family = AF_INET6;
    result.sin6_port = htons(port);
    result.sin6_scope_id = scope;
    std::memcpy(&result.sin6_addr, address.data(), address.size());
    return result;
}
bool linkLocal(const Address& address) { return address[0] == 0xfe && (address[1] & 0xc0) == 0x80; }
struct Socket
{
    int fd = -1;
    ~Socket() { close(); }
    void close()
    {
        if (fd >= 0) ::close(fd);
        fd = -1;
    }
    bool open(const Address& address, uint16_t port, uint32_t scope, bool multicast = false)
    {
        close();
        fd = ::socket(AF_INET6, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) return false;
        int one = 1, hops = 1;
        bool ok = setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one)) == 0 &&
                  setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &one, sizeof(one)) == 0;
        if (multicast)
        {
            ipv6_mreq membership{};
            std::memcpy(&membership.ipv6mr_multiaddr, address.data(), address.size());
            membership.ipv6mr_interface = scope;
            ok &= setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == 0;
            ok &= setsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &membership, sizeof(membership)) == 0;
#if defined(IPV6_MULTICAST_ALL)
            int zero = 0;
            ok &= setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_ALL, &zero, sizeof(zero)) == 0;
#endif
        }
        ok &= setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, &scope, sizeof(scope)) == 0;
        ok &= setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof(hops)) == 0;
        const auto local = endpoint(address, port, scope);
        ok &= ::bind(fd, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == 0;
        if (!ok) close();
        return ok;
    }
    bool send(const Address& destination, uint16_t port, uint32_t scope, const uint8_t* bytes, size_t size) const
    {
        const auto target = endpoint(destination, port, scope);
        return fd >= 0 && ::sendto(fd, bytes, size, MSG_DONTWAIT, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == ssize_t(size);
    }
};
} // namespace
bool LinuxReticulumAuto::discoveryAddress(const char* group, Address& output)
{
    if (!groupValid(group)) return false;
    std::array<uint8_t, 32> hash{};
    digest(reinterpret_cast<const uint8_t*>(group), std::strlen(group), hash.data());
    output = {};
    output[0] = 0xff;
    output[1] = 0x12;
    std::copy_n(hash.data() + 2, 12, output.begin() + 4);
    return true;
}
bool LinuxReticulumAuto::discoveryToken(const char* group, const Address& address, std::array<uint8_t, 32>& output)
{
    if (!groupValid(group) || !linkLocal(address)) return false;
    char text[INET6_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET6, address.data(), text, sizeof(text))) return false;
    SHA256 hash;
    hash.update(group, std::strlen(group));
    hash.update(text, std::strlen(text));
    hash.finalize(output.data(), output.size());
    return true;
}
struct LinuxReticulumAuto::Impl
{
    struct Local
    {
        uint32_t scope = 0;
        Address address{};
        Socket multicast, discovery, data;
        uint32_t announced_at = 0;
        bool announced = false;
    };
    struct Remote
    {
        Peer view;
        uint32_t heard_at = 0, reversed_at = 0;
        bool reversed = false;
    };
    struct Seen
    {
        std::array<uint8_t, 32> hash{};
        uint32_t at = 0;
        bool used = false;
    };
    std::array<std::unique_ptr<Local>, kMaxLocalInterfaces> locals;
    std::array<Remote, kMaxPeers> peers{};
    std::array<Frame, 16> rx{};
    std::array<Seen, 48> duplicates{};
    size_t head = 0, count = 0, duplicate_next = 0;
    size_t poll_start = 0;
    std::unique_ptr<r::InterfaceAccessCodec> access;
    r::NetworkInterfaceConfig config;
    Address multicast{};
    bool enabled = false, scanned = false;
    uint32_t now = 0, scanned_at = 0;
    uint64_t next_generation = 1;
    Snapshot status;
    Impl() { status.supported = true; }
    void error(const char* message) { std::snprintf(status.error, sizeof(status.error), "%s", message); }
    void closeLocal(size_t index)
    {
        if (!locals[index]) return;
        const auto scope = locals[index]->scope;
        for (auto& peer : peers)
            if (peer.view.generation && peer.view.scope == scope) peer = {};
        locals[index].reset();
    }
    Local* local(uint32_t scope)
    {
        for (auto& entry : locals)
            if (entry && entry->scope == scope) return entry.get();
        return nullptr;
    }
    Remote* remote(uint32_t scope, const Address& address)
    {
        for (auto& peer : peers)
            if (peer.view.generation && peer.view.scope == scope && peer.view.address == address) return &peer;
        return nullptr;
    }
    Remote* remote(const char* id, uint64_t generation)
    {
        if (!id || !generation || !enabled || std::strcmp(id, config.id)) return nullptr;
        for (auto& peer : peers)
            if (peer.view.generation == generation) return &peer;
        return nullptr;
    }
    void scan()
    {
        struct Candidate
        {
            uint32_t scope;
            Address address;
        };
        std::array<Candidate, kMaxLocalInterfaces> candidates{};
        size_t used = 0;
        ifaddrs* addresses = nullptr;
        if (getifaddrs(&addresses) != 0)
        {
            for (size_t i = 0; i < locals.size(); ++i) closeLocal(i);
            error("Cannot enumerate IPv6 interfaces");
            return;
        }
        for (auto* entry = addresses; entry; entry = entry->ifa_next)
        {
            if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET6 ||
                !(entry->ifa_flags & IFF_UP) || !(entry->ifa_flags & IFF_MULTICAST) || (entry->ifa_flags & IFF_LOOPBACK)) continue;
            Address address{};
            std::memcpy(address.data(), &reinterpret_cast<sockaddr_in6*>(entry->ifa_addr)->sin6_addr, address.size());
            const auto scope = if_nametoindex(entry->ifa_name);
            if (!scope || !linkLocal(address)) continue;
            size_t slot = 0;
            while (slot < used && candidates[slot].scope != scope) ++slot;
            if (slot < used)
            {
                if (address < candidates[slot].address) candidates[slot].address = address;
            }
            else if (used < candidates.size()) candidates[used++] = {scope, address};
        }
        freeifaddrs(addresses);
        for (size_t i = 0; i < locals.size(); ++i)
        {
            if (!locals[i]) continue;
            bool retained = false;
            for (size_t j = 0; j < used; ++j) retained |= locals[i]->scope == candidates[j].scope && locals[i]->address == candidates[j].address;
            if (!retained) closeLocal(i);
        }
        status.error[0] = 0;
        for (size_t j = 0; j < used; ++j)
        {
            if (local(candidates[j].scope)) continue;
            auto entry = std::make_unique<Local>();
            entry->scope = candidates[j].scope;
            entry->address = candidates[j].address;
            if (!entry->multicast.open(multicast, config.discovery_port, entry->scope, true) ||
                !entry->discovery.open(entry->address, uint16_t(config.discovery_port + 1), entry->scope) ||
                !entry->data.open(entry->address, config.data_port, entry->scope))
            {
                error("Cannot bind AutoInterface UDP sockets");
                continue;
            }
            for (auto& slot : locals)
                if (!slot)
                {
                    slot = std::move(entry);
                    break;
                }
        }
        if (!used) error("No multicast-capable link-local IPv6 interface");
    }
    void observe(Local& local, const Address& source, const uint8_t* bytes, size_t size)
    {
        std::array<uint8_t, 32> expected{};
        if (size != expected.size() || !LinuxReticulumAuto::discoveryToken(config.group_id, source, expected) ||
            std::memcmp(bytes, expected.data(), expected.size()))
        {
            ++status.rejected;
            return;
        }
        for (const auto& own : locals)
            if (own && own->address == source) return;
        if (auto* peer = remote(local.scope, source))
        {
            peer->heard_at = now;
            return;
        }
        for (auto& peer : peers)
            if (!peer.view.generation)
            {
                peer.view.generation = next_generation++;
                if (!next_generation) ++next_generation;
                peer.view.scope = local.scope;
                peer.view.address = source;
                std::snprintf(peer.view.id, sizeof(peer.view.id), "%s", config.id);
                peer.heard_at = now;
                ++status.discovered;
                return;
            }
        ++status.peer_limit;
    }
    void incoming(Local& local, const Address& source, uint8_t* bytes, size_t size)
    {
        auto* peer = remote(local.scope, source);
        if (!peer || !access)
        {
            ++status.rejected;
            return;
        }
        std::array<uint8_t, 32> hash{};
        digest(bytes, size, hash.data());
        if (!access->decode(bytes, size, kWireBytes) || size > r::kReticulumMtu)
        {
            ++status.rejected;
            return;
        }
        for (const auto& entry : duplicates)
            if (entry.used && uint32_t(now - entry.at) < kDuplicateMs && entry.hash == hash)
            {
                ++status.duplicate_rx;
                return;
            }
        peer->heard_at = now;
        if (count == rx.size())
        {
            ++status.discarded_rx;
            return;
        }
        duplicates[duplicate_next] = {hash, now, true};
        duplicate_next = (duplicate_next + 1) % duplicates.size();
        auto& frame = rx[(head + count) % rx.size()];
        std::snprintf(frame.interface_id, sizeof(frame.interface_id), "%s", config.id);
        frame.connection_generation = peer->view.generation;
        frame.size = size;
        std::memcpy(frame.data.data(), bytes, size);
        ++count;
        ++status.received;
    }
    void poll()
    {
        struct Input
        {
            Local* local;
            bool data;
        };
        std::array<pollfd, kMaxLocalInterfaces * 3> sockets{};
        std::array<Input, kMaxLocalInterfaces * 3> inputs{};
        size_t used = 0;
        for (auto& entry : locals)
            if (entry)
                for (const auto descriptor : {entry->multicast.fd, entry->discovery.fd, entry->data.fd})
                {
                    sockets[used] = {descriptor, POLLIN, 0};
                    inputs[used++] = {entry.get(), descriptor == entry->data.fd};
                }
        if (!used || ::poll(sockets.data(), used, 0) <= 0) return;
        unsigned packets = 0;
        size_t budget = 8192;
        const auto first = poll_start % used;
        poll_start = (first + 1) % used;
        for (size_t step = 0; step < used && packets < 64 && budget; ++step)
        {
            const auto i = (first + step) % used;
            if (!(sockets[i].revents & POLLIN)) continue;
            unsigned local_packets = 0;
            while (local_packets < 8 && packets < 64 && budget)
            {
                std::array<uint8_t, kWireBytes> bytes{};
                sockaddr_in6 source{};
                alignas(cmsghdr) unsigned char control[CMSG_SPACE(sizeof(in6_pktinfo))]{};
                iovec vector{bytes.data(), bytes.size()};
                msghdr message{};
                message.msg_name = &source;
                message.msg_namelen = sizeof(source);
                message.msg_iov = &vector;
                message.msg_iovlen = 1;
                message.msg_control = control;
                message.msg_controllen = sizeof(control);
                const auto length = recvmsg(sockets[i].fd, &message, MSG_DONTWAIT | MSG_TRUNC);
                if (length < 0) break;
                ++packets;
                ++local_packets;
                budget -= std::min(budget, size_t(length));
                uint32_t scope = 0;
                for (auto* item = CMSG_FIRSTHDR(&message); item; item = CMSG_NXTHDR(&message, item))
                    if (item->cmsg_level == IPPROTO_IPV6 && item->cmsg_type == IPV6_PKTINFO && item->cmsg_len >= CMSG_LEN(sizeof(in6_pktinfo)))
                        scope = reinterpret_cast<const in6_pktinfo*>(CMSG_DATA(item))->ipi6_ifindex;
                Address address{};
                std::memcpy(address.data(), &source.sin6_addr, address.size());
                if ((message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) || size_t(length) > bytes.size() ||
                    scope != inputs[i].local->scope || !linkLocal(address))
                {
                    ++status.rejected;
                    continue;
                }
                if (inputs[i].data) incoming(*inputs[i].local, address, bytes.data(), size_t(length));
                else observe(*inputs[i].local, address, bytes.data(), size_t(length));
            }
        }
    }
};
LinuxReticulumAuto::LinuxReticulumAuto() : impl_(std::make_unique<Impl>()) {}
LinuxReticulumAuto::~LinuxReticulumAuto() = default;
bool LinuxReticulumAuto::configuredAs(const r::ReticulumNetworkConfig& config) const
{
    if (!r::validateNetworkConfig(config)) return false;
    const r::NetworkInterfaceConfig* selected = nullptr;
    for (size_t i = 0; i < config.interface_count; ++i)
        if (config.interfaces[i].type == r::NetworkInterfaceType::Auto && config.interfaces[i].enabled) selected = &config.interfaces[i];
    if (!selected) return !impl_->enabled;
    const auto& current = impl_->config;
    return impl_->enabled && !std::strcmp(current.id, selected->id) && !std::strcmp(current.group_id, selected->group_id) &&
           current.discovery_port == selected->discovery_port && current.data_port == selected->data_port && current.access == selected->access;
}
bool LinuxReticulumAuto::configure(const r::ReticulumNetworkConfig& config)
{
    if (!r::validateNetworkConfig(config)) return false;
    const r::NetworkInterfaceConfig* selected = nullptr;
    for (size_t i = 0; i < config.interface_count; ++i)
        if (config.interfaces[i].type == r::NetworkInterfaceType::Auto && config.interfaces[i].enabled) selected = &config.interfaces[i];
    if (!selected)
    {
        clear();
        return true;
    }
    const auto& current = impl_->config;
    if (impl_->enabled && !std::strcmp(current.id, selected->id) && !std::strcmp(current.group_id, selected->group_id) &&
        current.discovery_port == selected->discovery_port && current.data_port == selected->data_port && current.access == selected->access) return true;
    auto access = std::make_unique<r::InterfaceAccessCodec>();
    Address group{};
    if (!access->configure(selected->access.network_name, selected->access.passphrase, selected->access.ifac_size_bits / 8) ||
        !discoveryAddress(selected->group_id, group)) return false;
    clear();
    impl_->config = *selected;
    impl_->multicast = group;
    impl_->access = std::move(access);
    impl_->enabled = true;
    return true;
}
void LinuxReticulumAuto::tick(uint32_t now)
{
    auto& self = *impl_;
    self.now = now;
    if (!self.enabled) return;
    if (!self.scanned || uint32_t(now - self.scanned_at) >= kScanMs)
    {
        self.scan();
        self.scanned_at = now;
        self.scanned = true;
    }
    for (auto& peer : self.peers)
        if (peer.view.generation && uint32_t(now - peer.heard_at) > kPeerTimeoutMs)
        {
            peer = {};
            ++self.status.expired;
        }
    self.poll();
    for (auto& local : self.locals)
        if (local)
        {
            std::array<uint8_t, 32> token{};
            if (!discoveryToken(self.config.group_id, local->address, token)) continue;
            if (!local->announced || uint32_t(now - local->announced_at) >= kAnnounceMs)
            {
                (void)local->discovery.send(self.multicast, self.config.discovery_port, local->scope, token.data(), token.size());
                local->announced = true;
                local->announced_at = now;
            }
            for (auto& peer : self.peers)
                if (peer.view.generation && peer.view.scope == local->scope &&
                    (!peer.reversed || uint32_t(now - peer.reversed_at) >= kReverseMs))
                {
                    (void)local->discovery.send(peer.view.address, uint16_t(self.config.discovery_port + 1), local->scope, token.data(), token.size());
                    peer.reversed = true;
                    peer.reversed_at = now;
                }
        }
}
bool LinuxReticulumAuto::online(const char* id, uint64_t generation) const { return impl_->remote(id, generation) != nullptr; }
bool LinuxReticulumAuto::send(const char* id, uint64_t generation, const uint8_t* bytes, size_t size)
{
    auto* peer = impl_->remote(id, generation);
    if (!peer || !bytes || size > r::kReticulumMtu || !impl_->access) return false;
    auto* local = impl_->local(peer->view.scope);
    if (!local) return false;
    std::array<uint8_t, kWireBytes> packet{};
    std::memcpy(packet.data(), bytes, size);
    if (!impl_->access->encode(packet.data(), size, packet.size()) ||
        !local->data.send(peer->view.address, impl_->config.data_port, local->scope, packet.data(), size)) return false;
    ++impl_->status.transmitted;
    return true;
}
bool LinuxReticulumAuto::receive(Frame& output)
{
    while (impl_->count)
    {
        auto& frame = impl_->rx[impl_->head];
        const bool current = online(frame.interface_id, frame.connection_generation);
        if (current) output = frame;
        frame = {};
        impl_->head = (impl_->head + 1) % impl_->rx.size();
        --impl_->count;
        if (current) return true;
        ++impl_->status.discarded_rx;
    }
    return false;
}
void LinuxReticulumAuto::clear()
{
    for (size_t i = 0; i < impl_->locals.size(); ++i) impl_->closeLocal(i);
    impl_->peers = {};
    impl_->rx = {};
    impl_->duplicates = {};
    impl_->head = impl_->count = impl_->duplicate_next = 0;
    impl_->access.reset();
    impl_->enabled = impl_->scanned = false;
    impl_->status.error[0] = 0;
}
LinuxReticulumAuto::Snapshot LinuxReticulumAuto::snapshot() const
{
    auto result = impl_->status;
    result.count = result.local_interfaces = 0;
    for (const auto& local : impl_->locals)
        if (local) ++result.local_interfaces;
    for (const auto& peer : impl_->peers)
        if (peer.view.generation) result.peers[result.count++] = peer.view;
    return result;
}
} // namespace trailmate::linux_app
