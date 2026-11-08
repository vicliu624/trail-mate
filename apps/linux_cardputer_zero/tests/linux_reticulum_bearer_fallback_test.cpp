#include "chat/infra/reticulum/reticulum_wire.h"
#include "chat/infra/rnode/rnode_packet_wire.h"
#include "chat/linux_mesh_worker.h"
#include "chat/linux_reticulum_announce.h"
#include "chat/ports/i_mesh_peer_directory.h"
#include "platform/linux/packet_radio.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "sys/event_bus.h"
#include <arpa/inet.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace native = platform::linux_runtime;
namespace r = chat::reticulum;
using Worker = trailmate::linux_app::LinuxMeshWorker;
using Announce = trailmate::linux_app::LinuxReticulumAnnounce;
using namespace std::chrono_literals;

struct Radio final : native::IPacketRadio
{
    std::atomic<bool> online{false};
    std::atomic<unsigned> requests{0};
    std::array<uint8_t, 16> target{};
    std::mutex mutex;
    std::deque<native::RadioPacket> incoming;
    native::RadioSettings settings;
    chat::rnode::ReassemblyState decoder;
    const char* name() const override { return "Fallback fixture LoRa"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override
    {
        online = true;
        return true;
    }
    void release() override { online = false; }
    bool configureLoRa(const native::RadioSettings& value) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        settings = value;
        return true;
    }
    bool startReceive() override { return online; }
    bool transmit(const uint8_t* bytes, size_t size) override
    {
        uint8_t plain[r::kReticulumMtu];
        size_t length = sizeof(plain);
        bool complete = false;
        if (chat::rnode::feedAirPacket(&decoder, bytes, size, plain, &length, &complete) && complete)
        {
            r::ParsedPacket packet;
            assert(r::parsePacket(plain, length, &packet));
            if (packet.packet_type == r::PacketType::LinkRequest)
            {
                assert(!std::memcmp(packet.destination_hash, target.data(), target.size()));
                ++requests;
            }
        }
        return true;
    }
    bool pollReceive(native::RadioPacket* out) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (incoming.empty()) return false;
        *out = incoming.front();
        incoming.pop_front();
        return true;
    }
    void inject(const Announce& announce)
    {
        chat::rnode::EncodedAirPacketSet air;
        assert(chat::rnode::encodeAirPacketSet(announce.data(), announce.size(), 7, &air));
        std::lock_guard<std::mutex> lock(mutex);
        native::RadioPacket packet;
        packet.size = air.first_len;
        std::memcpy(packet.data.data(), air.first, packet.size);
        incoming.push_back(packet);
        if (air.count == 2)
        {
            packet.size = air.second_len;
            std::memcpy(packet.data.data(), air.second, packet.size);
            incoming.push_back(packet);
        }
    }
    float readRssi() override { return -60; }
    bool isOnline() const override { return online; }
    const char* lastError() const override { return ""; }
    native::RadioSettings appliedLoRaConfig() const override { return settings; }
    native::RadioStatistics stats() const override { return {}; }
};

struct Socket
{
    int listener = -1, peer = -1;
    uint16_t port;
    bool active = false, escape = false;
    std::vector<uint8_t> buffer;
    Socket()
    {
        listener = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        assert(listener >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && listen(listener, 2) == 0);
        socklen_t size = sizeof(address);
        assert(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0);
        port = ntohs(address.sin_port);
    }
    void disconnect()
    {
        if (peer >= 0)
        {
            shutdown(peer, SHUT_RDWR);
            close(peer);
            peer = -1;
        }
    }
    ~Socket()
    {
        disconnect();
        close(listener);
    }
    void write(const Announce& announce)
    {
        std::vector<uint8_t> bytes{0x7e};
        for (size_t i = 0; i < announce.size(); ++i)
        {
            const auto value = announce.data()[i];
            if (value == 0x7d || value == 0x7e)
            {
                bytes.push_back(0x7d);
                bytes.push_back(value ^ 0x20);
            }
            else bytes.push_back(value);
        }
        bytes.push_back(0x7e);
        assert(send(peer, bytes.data(), bytes.size(), MSG_NOSIGNAL) == ssize_t(bytes.size()));
    }
    bool sawLinkRequest()
    {
        uint8_t bytes[4096];
        const auto count = recv(peer, bytes, sizeof(bytes), MSG_DONTWAIT);
        if (count < 0)
        {
            assert(errno == EAGAIN || errno == EWOULDBLOCK);
            return false;
        }
        bool found = false;
        for (ssize_t i = 0; i < count; ++i)
        {
            const auto value = bytes[i];
            if (value == 0x7e)
            {
                if (active && !buffer.empty())
                {
                    r::ParsedPacket packet;
                    assert(r::parsePacket(buffer.data(), buffer.size(), &packet));
                    found |= packet.packet_type == r::PacketType::LinkRequest;
                }
                active = true;
                escape = false;
                buffer.clear();
            }
            else if (active)
            {
                if (escape)
                {
                    buffer.push_back(value ^ 0x20);
                    escape = false;
                }
                else if (value == 0x7d) escape = true;
                else buffer.push_back(value);
            }
        }
        return found;
    }
};

int main(int argc, char** argv)
{
    const bool wifi_only = argc > 1 && !std::strcmp(argv[1], "--wifi-only");
    const bool mute_during_send = argc > 1 && !std::strcmp(argv[1], "--mute");
    const bool expect_failure = wifi_only || mute_during_send;
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-bearer-fallback-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    sys::EventBus::init(64);
    {
        native::LinuxReticulumIdentity remote(root / "remote.sqlite");
        assert(remote.begin());
        Announce announcement(remote);
        assert(announcement.build(Announce::Aspect::Delivery, "Fallback peer", 12345));
        Radio radio;
        radio.target = remote.destinationHash();
        Socket server;
        Worker worker(0x10203040, &radio);
        chat::MeshConfig config;
        config.reticulum_interface_policy = wifi_only ? chat::ReticulumInterfacePolicy::WifiGatewayOnly : chat::ReticulumInterfacePolicy::All;
        config.reticulum_lora_enabled = config.reticulum_wifi_gateway_enabled = true;
        chat::reticulum::ReticulumNetworkConfig network;
        network.propagation.enabled = false;
        network.interface_count = 1;
        auto& tcp = network.interfaces[0];
        tcp.enabled = true;
        tcp.type = r::NetworkInterfaceType::TcpClient;
        std::strcpy(tcp.id, "fallback-tcp");
        std::strcpy(tcp.target_host, "127.0.0.1");
        tcp.target_port = server.port;
        worker.applyProtocolConfig(chat::MeshProtocol::Reticulum, config);
        worker.applyReticulumNetworkConfig(network);
        assert(worker.initialize());
        const auto target = chat::makeReticulumPeerIdentity(remote.destinationHash().data(), remote.identityHash().data());
        bool announced = false, submitted = false, disconnected = false, failed = false;
        constexpr chat::MessageId message_id = 0x33445566;
        const auto deadline = std::chrono::steady_clock::now() + 12s;
        while (std::chrono::steady_clock::now() < deadline && !radio.requests && !failed)
        {
            worker.tick();
            if (!announced)
            {
                server.peer = accept4(server.listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (server.peer >= 0)
                {
                    server.write(announcement);
                    announced = true;
                }
                else assert(errno == EAGAIN || errno == EWOULDBLOCK);
            }
            if (announced && !submitted)
            {
                chat::MeshPeerRecord peer;
                chat::MeshTransportStatus transport;
                worker.getTransportStatus(&transport);
                if ((wifi_only || transport.radio_ready) && transport.network_ready &&
                    platform::ui::reticulum_directory::mesh_peer_directory().find(chat::makeMeshPeerReticulumIdentity(target), peer).succeeded() && peer.reticulum.has_public_keys)
                {
                    assert(worker.sendTextToReticulumDestination(chat::ChannelId::PRIMARY, "preserve this original text", message_id, target).ok);
                    submitted = true;
                }
            }
            if (submitted && !disconnected && server.sawLinkRequest())
            {
                if (mute_during_send)
                {
                    config.tx_enabled = false;
                    worker.applyProtocolConfig(chat::MeshProtocol::Reticulum, config);
                }
                radio.inject(announcement);
                server.disconnect();
                disconnected = true;
            }
            Worker::Outcome result;
            while (worker.takeOutcome(result))
                if (result.message == message_id && result.status == chat::MessageStatus::Failed)
                {
                    failed = true;
                    assert(expect_failure);
                }
            std::this_thread::sleep_for(2ms);
        }
        assert(announced && submitted && disconnected);
        if (expect_failure) assert(failed && radio.requests == 0);
        else assert(!failed && radio.requests == 1);
        worker.shutdown();
    }
    std::filesystem::remove_all(root);
}
