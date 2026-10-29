#include "chat/infra/mesh_peer_directory_core.h"
#include "chat/linux_reticulum_auto.h"
#include "chat/linux_reticulum_network.h"
#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace r = chat::reticulum;
using Auto = trailmate::linux_app::LinuxReticulumAuto;
using Network = trailmate::linux_app::LinuxReticulumNetwork;
struct Store : chat::IMeshPeerDirectoryBlobStore
{
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(std::vector<uint8_t>&) override { return chat::MeshPeerDirectoryBlobLoadResult::Missing; }
    bool saveBlob(const uint8_t*, size_t) override { return true; }
    void clearBlob() override {}
};
static std::string unhex(const std::string& hex)
{
    assert(hex.size() % 2 == 0);
    std::string output;
    for (size_t i = 0; i < hex.size(); i += 2) output.push_back(char(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return output;
}
static uint32_t now()
{
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
static r::ReticulumNetworkConfig configuration(const char* group, uint16_t discovery, uint16_t data, unsigned bits)
{
    r::ReticulumNetworkConfig config;
    config.interface_count = 1;
    auto& entry = config.interfaces[0];
    std::strcpy(entry.id, "dynamic-lan");
    entry.type = r::NetworkInterfaceType::Auto;
    entry.enabled = true;
    std::snprintf(entry.group_id, sizeof(entry.group_id), "%s", group);
    entry.discovery_port = discovery;
    entry.data_port = data;
    if (bits)
    {
        std::strcpy(entry.access.network_name, "trailmate-auto");
        std::strcpy(entry.access.passphrase, "interop-secret");
        entry.access.ifac_size_bits = uint16_t(bits);
    }
    return config;
}
static int interop(const char* group, uint16_t discovery, uint16_t data, unsigned bits)
{
    auto config = configuration(group, discovery, data, bits);
    Auto transport;
    assert(transport.configure(config));
    std::array<uint8_t, 500> packet{};
    packet[0] = 0x14;
    for (size_t i = 2; i < packet.size(); ++i) packet[i] = uint8_t(i);
    packet[19] = 1;
    uint64_t first = 0, second = 0;
    bool first_echo = false, second_echo = false, readdress_requested = false;
    bool sent = false;
    uint32_t sent_at = 0;
    const auto started = now();
    while (uint32_t(now() - started) < 18000)
    {
        const auto time = now();
        transport.tick(time);
        const auto snapshot = transport.snapshot();
        if (snapshot.count)
        {
            const auto& peer = snapshot.peers[0];
            if (!first) first = peer.generation;
            if (readdress_requested && peer.generation != first) second = peer.generation;
            const auto selected = first_echo ? second : first;
            if (selected && (!sent || uint32_t(time - sent_at) > 1000))
            {
                assert(transport.send(peer.id, selected, packet.data(), packet.size()));
                sent = true;
                sent_at = time;
            }
        }
        Auto::Frame frame;
        while (transport.receive(frame))
        {
            auto expected = packet;
            expected[20] ^= 0x80;
            assert(frame.size == expected.size() && !std::memcmp(frame.data.data(), expected.data(), expected.size()));
            if (!first_echo) first_echo = true;
            else if (second && frame.connection_generation == second) second_echo = true;
        }
        if (first_echo && !readdress_requested && transport.snapshot().duplicate_rx)
        {
            auto invalid = config;
            invalid.interfaces[0].data_port = invalid.interfaces[0].discovery_port;
            assert(!transport.configure(invalid) && transport.online("dynamic-lan", first));
            readdress_requested = true;
            sent = false;
            packet[19] = 2;
            std::puts("READY_READDRESS");
            std::fflush(stdout);
        }
        if (second_echo)
        {
            assert(!transport.online("dynamic-lan", first));
            transport.tick(time + 23000);
            assert(!transport.online("dynamic-lan", second) && transport.snapshot().expired);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(first_echo && second_echo && first != second);
    assert(transport.snapshot().rejected && transport.snapshot().duplicate_rx);
    transport.clear();
    assert(!transport.snapshot().count && !transport.send("dynamic-lan", first, packet.data(), packet.size()));

    // Exercise the production network owner, not just the UDP bearer: its
    // identity announcement and path-request broadcast must select Auto peers.
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-auto-network-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    {
        platform::linux_runtime::LinuxReticulumIdentity identity(root / "state.sqlite3");
        assert(identity.begin());
        Store store;
        chat::MeshPeerDirectoryCore directory(store);
        Network network(identity, directory, {});
        assert(network.configure(config, false, "Auto integration"));
        bool routed = false;
        uint8_t target[16]{};
        target[0] = 0x77;
        const auto began = now();
        while (uint32_t(now() - began) < 10000)
        {
            network.tick(now(), uint32_t(std::time(nullptr)));
            if (!routed && network.ready() && network.snapshot().announcements && network.requestPath(target)) routed = true;
            if (routed && network.snapshot().links.active) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(routed && network.snapshot().links.active && network.snapshot().automatic.count && !network.snapshot().tcp.count);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::filesystem::remove_all(root);
    std::puts("AUTO_INTEROP_PASSED");
    return 0;
}
int main(int argc, char** argv)
{
    if (argc == 6 && !std::strcmp(argv[1], "--peer"))
        return interop(argv[2], uint16_t(std::stoul(argv[3])), uint16_t(std::stoul(argv[4])), unsigned(std::stoul(argv[5])));
    assert(argc == 2);
    std::ifstream input(argv[1]);
    assert(input);
    std::string line;
    unsigned vectors = 0;
    while (std::getline(input, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        std::string group_hex, address_text, multicast_hex, token_hex;
        assert(fields >> group_hex >> address_text >> multicast_hex >> token_hex);
        const auto group = unhex(group_hex), multicast = unhex(multicast_hex), token = unhex(token_hex);
        Auto::Address address{}, group_address{};
        assert(inet_pton(AF_INET6, address_text.c_str(), address.data()) == 1);
        std::array<uint8_t, 32> output{};
        assert(Auto::discoveryAddress(group.c_str(), group_address) && !std::memcmp(group_address.data(), multicast.data(), 16));
        assert(Auto::discoveryToken(group.c_str(), address, output) && !std::memcmp(output.data(), token.data(), 32));
        ++vectors;
    }
    assert(vectors == 9);
    Auto::Address address{};
    std::array<uint8_t, 32> token{};
    assert(!Auto::discoveryToken("reticulum", address, token));
    assert(!Auto::discoveryAddress("", address));
    auto config = configuration("reticulum", 29716, 42671, 128);
    Auto owner;
    assert(owner.configure(config) && owner.configuredAs(config));
    auto invalid = config;
    invalid.interfaces[0].discovery_port = 65535;
    assert(!owner.configure(invalid) && owner.configuredAs(config));
    invalid = config;
    invalid.interfaces[0].data_port = 29717;
    assert(!owner.configure(invalid) && owner.configuredAs(config));
    owner.clear();
    assert(!owner.configuredAs(config) && !owner.snapshot().count);
    std::puts("AutoInterface reference vectors, bounded configuration, invalid updates and clear passed");
}
