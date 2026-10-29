#include "chat/infra/reticulum/interface_access_codec.h"
#include "chat/linux_reticulum_tcp.h"
#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace r = chat::reticulum;
using Tcp = trailmate::linux_app::LinuxReticulumTcp;
using namespace std::chrono_literals;
struct Listener
{
    int fd = -1;
    uint16_t port = 0;
    std::vector<int> peers;
    explicit Listener(bool ipv6 = false)
    {
        fd = socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        assert(fd >= 0);
        int enabled = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
        if (ipv6)
        {
            sockaddr_in6 address{};
            address.sin6_family = AF_INET6;
            address.sin6_addr = in6addr_loopback;
            assert(bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
            socklen_t size = sizeof(address);
            assert(getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0);
            port = ntohs(address.sin6_port);
        }
        else
        {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            assert(bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
            socklen_t size = sizeof(address);
            assert(getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0);
            port = ntohs(address.sin_port);
        }
        assert(listen(fd, 8) == 0);
    }
    ~Listener()
    {
        for (int peer : peers)
            if (peer >= 0) close(peer);
        close(fd);
    }
    void acceptPending()
    {
        for (;;)
        {
            const int peer = accept4(fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (peer < 0)
            {
                assert(errno == EAGAIN || errno == EWOULDBLOCK);
                return;
            }
            peers.push_back(peer);
        }
    }
    void disconnect(size_t index)
    {
        assert(index < peers.size() && peers[index] >= 0);
        shutdown(peers[index], SHUT_RDWR);
        close(peers[index]);
        peers[index] = -1;
    }
};
std::vector<uint8_t> readAvailable(int fd)
{
    std::vector<uint8_t> result;
    uint8_t bytes[4096];
    for (;;)
    {
        const auto count = recv(fd, bytes, sizeof(bytes), MSG_DONTWAIT);
        if (count < 0)
        {
            assert(errno == EAGAIN || errno == EWOULDBLOCK);
            break;
        }
        if (!count) break;
        result.insert(result.end(), bytes, bytes + count);
        assert(result.size() < 128 * 1024);
    }
    return result;
}
void writeBytes(int fd, const std::vector<uint8_t>& bytes)
{
    assert(send(fd, bytes.data(), bytes.size(), MSG_DONTWAIT | MSG_NOSIGNAL) == ssize_t(bytes.size()));
}
std::vector<uint8_t> framed(const std::vector<uint8_t>& bytes)
{
    std::vector<uint8_t> result{0x7e};
    for (uint8_t byte : bytes)
    {
        if (byte == 0x7e || byte == 0x7d)
        {
            result.push_back(0x7d);
            result.push_back(byte ^ 0x20);
        }
        else result.push_back(byte);
    }
    result.push_back(0x7e);
    return result;
}
std::vector<uint8_t> protectedFrame(const std::vector<uint8_t>& plain, const char* passphrase = "access-fixture")
{
    r::InterfaceAccessCodec codec;
    assert(codec.configure("network-fixture", passphrase, 8));
    std::array<uint8_t, 564> bytes;
    std::memcpy(bytes.data(), plain.data(), plain.size());
    size_t size = plain.size();
    assert(codec.encode(bytes.data(), size, bytes.size()));
    return framed({bytes.begin(), bytes.begin() + size});
}
r::ReticulumNetworkConfig configuration(const Listener& alpha, const Listener& beta)
{
    r::ReticulumNetworkConfig config;
    config.interface_count = 2;
    for (unsigned index = 0; index < 2; ++index)
    {
        auto& endpoint = config.interfaces[index];
        endpoint.type = r::NetworkInterfaceType::TcpClient;
        endpoint.enabled = true;
        std::snprintf(endpoint.id, sizeof(endpoint.id), "%s", index ? "beta" : "alpha");
        std::snprintf(endpoint.target_host, sizeof(endpoint.target_host), "%s", index ? "localhost" : "127.0.0.1");
        endpoint.target_port = index ? beta.port : alpha.port;
    }
    auto& access = config.interfaces[1].access;
    std::snprintf(access.network_name, sizeof(access.network_name), "network-fixture");
    std::snprintf(access.passphrase, sizeof(access.passphrase), "access-fixture");
    access.ifac_size_bits = 64;
    return config;
}
Tcp::Endpoint endpoint(const Tcp& tcp, const char* id)
{
    const auto snapshot = tcp.snapshot();
    for (size_t index = 0; index < snapshot.count; ++index)
        if (!std::strcmp(snapshot.endpoints[index].id, id)) return snapshot.endpoints[index];
    assert(false);
    return {};
}
template <class Step, class Predicate>
void until(Step step, Predicate ready)
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!ready())
    {
        assert(std::chrono::steady_clock::now() < deadline);
        step();
    }
}
size_t descriptors()
{
    return size_t(std::distance(std::filesystem::directory_iterator("/proc/self/fd"), std::filesystem::directory_iterator{}));
}
int main()
{
    {
        Tcp initialize;
        assert(initialize.snapshot().supported);
    }
    const auto initial_fds = descriptors();
    // An ambient HTTP proxy must not capture a raw Reticulum connection.
    setenv("ALL_PROXY", "http://127.0.0.1:1", 1);
    setenv("http_proxy", "http://127.0.0.1:1", 1);
    {
        Listener alpha, beta;
        Tcp::Options options;
        options.read_byte_budget = 5;
        options.write_byte_budget = 7;
        Tcp tcp(options);
        auto config = configuration(alpha, beta);
        assert(tcp.configure(config));
        uint32_t now = UINT32_MAX - 200;
        const auto step = [&]
        { now += 2; tcp.tick(now); alpha.acceptPending(); beta.acceptPending(); std::this_thread::sleep_for(1ms); };
        until(step, [&]
              { return endpoint(tcp, "alpha").state == Tcp::State::Connected && endpoint(tcp, "beta").state == Tcp::State::Connected && alpha.peers.size() == 1 && beta.peers.size() == 1; });
        const auto epoch_a = endpoint(tcp, "alpha").generation, epoch_b = endpoint(tcp, "beta").generation;
        std::vector<uint8_t> plain(20);
        for (size_t index = 0; index < plain.size(); ++index) plain[index] = uint8_t(index * 7);
        plain[2] = 0x7d;
        plain[3] = 0x7e;
        assert(!tcp.send("alpha", 0, plain.data(), plain.size()) && !tcp.send("alpha", epoch_b, plain.data(), plain.size()));
        assert(!tcp.send("missing", epoch_a, plain.data(), plain.size()));
        assert(tcp.send("alpha", epoch_a, plain.data(), plain.size()) && tcp.send("beta", epoch_b, plain.data(), plain.size()));
        std::vector<uint8_t> a_bytes, b_bytes;
        until(step, [&]
              {
            auto a = readAvailable(alpha.peers[0]), b = readAvailable(beta.peers[0]);
            a_bytes.insert(a_bytes.end(), a.begin(), a.end()); b_bytes.insert(b_bytes.end(), b.begin(), b.end());
            return a_bytes.size() == framed(plain).size() && b_bytes.size() == protectedFrame(plain).size(); });
        assert(a_bytes == framed(plain) && b_bytes == protectedFrame(plain));
        const auto echo = [&](int fd, const std::vector<uint8_t>& bytes, const char* id, uint64_t generation)
        {
            for (const auto byte : bytes)
            {
                writeBytes(fd, {byte});
                step();
            }
            Tcp::Frame frame;
            until(step, [&]
                  { return tcp.receive(frame); });
            assert(!std::strcmp(frame.interface_id, id) && frame.connection_generation == generation && frame.size == plain.size() && !std::memcmp(frame.data.data(), plain.data(), plain.size()));
        };
        echo(alpha.peers[0], framed(plain), "alpha", epoch_a);
        echo(beta.peers[0], protectedFrame(plain), "beta", epoch_b);
        const auto small = plain;
        plain.assign(500, 0x7e);
        plain[0] = 0;
        echo(alpha.peers[0], framed(plain), "alpha", epoch_a);
        echo(beta.peers[0], protectedFrame(plain), "beta", epoch_b);
        assert(tcp.send("alpha", epoch_a, plain.data(), plain.size()));
        std::vector<uint8_t> large;
        until(step, [&]
              { const auto part = readAvailable(alpha.peers[0]); large.insert(large.end(), part.begin(), part.end()); return large.size() == framed(plain).size(); });
        assert(large == framed(plain));
        plain = small;
        const auto rejected = endpoint(tcp, "beta").rejected;
        writeBytes(beta.peers[0], protectedFrame(plain, "wrong-fixture"));
        writeBytes(beta.peers[0], framed(plain));
        until(step, [&]
              { return endpoint(tcp, "beta").rejected >= rejected + 2; });
        Tcp::Frame frame;
        assert(!tcp.receive(frame));
        echo(beta.peers[0], protectedFrame(plain), "beta", epoch_b);
        const auto before = endpoint(tcp, "alpha").rejected;
        std::vector<uint8_t> oversized(600, 1);
        oversized.insert(oversized.begin(), 0x7e);
        oversized.push_back(0x7e);
        writeBytes(alpha.peers[0], oversized);
        writeBytes(alpha.peers[0], {0x7e, 1, 2, 3, 0x7d, 0x7e});
        until(step, [&]
              { return endpoint(tcp, "alpha").rejected >= before + 2; });
        assert(!tcp.receive(frame));
        echo(alpha.peers[0], framed(plain), "alpha", epoch_a);
        auto invalid = config;
        invalid.interfaces[1].target_port = 0;
        assert(!tcp.configure(invalid) && tcp.online("alpha", epoch_a) && tcp.online("beta", epoch_b));
        invalid = config;
        std::memcpy(invalid.interfaces[1].id, invalid.interfaces[0].id, sizeof(invalid.interfaces[1].id));
        assert(!tcp.configure(invalid) && tcp.online("alpha", epoch_a));
        for (const char* host : {"bad host", "user@host", "host:4242", "tcp://host", "host/path", "[not-ipv6]"})
        {
            invalid = config;
            std::snprintf(invalid.interfaces[0].target_host, sizeof(invalid.interfaces[0].target_host), "%s", host);
            assert(!tcp.configure(invalid) && tcp.online("alpha", epoch_a));
        }
        assert(tcp.configure(config) && tcp.online("alpha", epoch_a));
        std::swap(config.interfaces[0], config.interfaces[1]);
        assert(tcp.configure(config) && tcp.online("alpha", epoch_a) && tcp.online("beta", epoch_b));
        // Retire queued RX and partially written TX when only alpha changes.
        const auto received_before = endpoint(tcp, "alpha").received;
        writeBytes(alpha.peers[0], framed(plain));
        until(step, [&]
              { return endpoint(tcp, "alpha").received > received_before; });
        assert(tcp.send("alpha", epoch_a, plain.data(), plain.size()));
        step();
        assert(endpoint(tcp, "alpha").pending_tx == 1);
        const auto partial = readAvailable(alpha.peers[0]);
        assert(!partial.empty() && partial.size() < framed(plain).size());
        std::snprintf(config.interfaces[1].target_host, sizeof(config.interfaces[1].target_host), "localhost");
        assert(tcp.configure(config) && !tcp.online("alpha", epoch_a) && tcp.online("beta", epoch_b));
        assert(!tcp.receive(frame) && tcp.snapshot().discarded_rx > 0);
        until(step, [&]
              { return endpoint(tcp, "alpha").state == Tcp::State::Connected && alpha.peers.size() == 2; });
        const auto next_a = endpoint(tcp, "alpha").generation;
        assert(next_a != epoch_a && !tcp.online("alpha", epoch_a));
        for (unsigned index = 0; index < 20; ++index) step();
        assert(readAvailable(alpha.peers[1]).empty());
        echo(alpha.peers[1], framed(plain), "alpha", next_a);
        for (unsigned index = 0; index < 8; ++index) assert(tcp.send("beta", epoch_b, plain.data(), plain.size()));
        assert(!tcp.send("beta", epoch_b, plain.data(), plain.size()));
        now += 5001;
        step();
        assert(!tcp.online("beta", epoch_b) && endpoint(tcp, "beta").discarded_tx == 8 && tcp.online("alpha", next_a));
        alpha.disconnect(1);
        until(step, [&]
              { return !tcp.online("alpha", next_a); });
        now += 10001;
        until(step, [&]
              { return endpoint(tcp, "alpha").state == Tcp::State::Connected && alpha.peers.size() == 3; });
        assert(endpoint(tcp, "alpha").generation != next_a);
        echo(alpha.peers[2], framed(plain), "alpha", endpoint(tcp, "alpha").generation);
        tcp.clear();
        assert(tcp.snapshot().count == 0 && !tcp.receive(frame));
    }
    {
        Listener alpha, beta;
        Tcp tcp;
        auto config = configuration(alpha, beta);
        assert(tcp.configure(config));
        uint32_t now = 0;
        const auto step = [&]
        { now += 2; tcp.tick(now); alpha.acceptPending(); beta.acceptPending(); std::this_thread::sleep_for(1ms); };
        until(step, [&]
              { return endpoint(tcp, "alpha").state == Tcp::State::Connected && endpoint(tcp, "beta").state == Tcp::State::Connected && !alpha.peers.empty(); });
        const auto a = endpoint(tcp, "alpha").generation, b = endpoint(tcp, "beta").generation;
        writeBytes(beta.peers[0], protectedFrame(std::vector<uint8_t>(20, 0)));
        Tcp::Frame warmup;
        until(step, [&]
              { return tcp.receive(warmup); });
        assert(!std::strcmp(warmup.interface_id, "beta"));
        const auto packet = framed(std::vector<uint8_t>(20, 0));
        std::vector<uint8_t> burst;
        for (unsigned index = 0; index < 33; ++index) burst.insert(burst.end(), packet.begin(), packet.end());
        writeBytes(alpha.peers[0], burst);
        writeBytes(beta.peers[0], protectedFrame(std::vector<uint8_t>(20, 0)));
        until(step, [&]
              { return !tcp.online("alpha", a); });
        assert(tcp.online("beta", b));
        Tcp::Frame frame;
        until(step, [&]
              { return tcp.receive(frame); });
        assert(!std::strcmp(frame.interface_id, "beta") && frame.size == 20);
        assert(!tcp.receive(frame) && tcp.snapshot().discarded_rx == 32);
    }
    {
        Listener server(true), unused;
        Tcp tcp;
        auto config = configuration(server, unused);
        config.interface_count = 1;
        std::snprintf(config.interfaces[0].target_host, sizeof(config.interfaces[0].target_host), "::1");
        assert(tcp.configure(config));
        uint32_t now = 0;
        const auto step = [&]
        { now += 2; tcp.tick(now); server.acceptPending(); std::this_thread::sleep_for(1ms); };
        until(step, [&]
              { return endpoint(tcp, "alpha").state == Tcp::State::Connected && !server.peers.empty(); });
        const auto packet = framed(std::vector<uint8_t>(20, 0));
        writeBytes(server.peers[0], packet);
        Tcp::Frame frame;
        until(step, [&]
              { return tcp.receive(frame); });
        assert(frame.size == 20);
    }
    {
        Listener alpha, beta;
        Tcp tcp;
        const auto config = configuration(alpha, beta);
        for (unsigned iteration = 0; iteration < 20; ++iteration)
        {
            assert(tcp.configure(config));
            tcp.tick(iteration * 20);
            const auto before = std::chrono::steady_clock::now();
            tcp.clear();
            assert(std::chrono::steady_clock::now() - before < 2s);
            alpha.acceptPending();
            beta.acceptPending();
        }
    }
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (descriptors() > initial_fds && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(5ms);
    assert(descriptors() == initial_fds);
}
