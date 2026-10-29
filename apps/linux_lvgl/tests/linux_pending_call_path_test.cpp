#include "call_pcm_fixture.h"
#include "chat/infra/mesh_peer_directory_core.h"
#include "chat/infra/reticulum/link_handshake_wire.h"
#include "chat/infra/reticulum/path_request_wire.h"
#include "chat/linux_mesh_worker.h"
#include "chat/linux_reticulum_announce.h"
#include "chat/linux_reticulum_network.h"
#include "platform/audio/audio_focus.h"
#include "platform/linux/music_service.h"
#include "platform/linux/native_audio_service.h"
#include "platform/linux/packet_radio.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "sys/event_bus.h"
#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace r = chat::reticulum;
namespace call = platform::ui::reticulum_call;
namespace native = platform::linux_runtime;
using Network = trailmate::linux_app::LinuxReticulumNetwork;
using Announce = trailmate::linux_app::LinuxReticulumAnnounce;
using Identity = native::LinuxReticulumIdentity;
struct Store : chat::IMeshPeerDirectoryBlobStore
{
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(std::vector<uint8_t>&) override { return chat::MeshPeerDirectoryBlobLoadResult::Missing; }
    bool saveBlob(const uint8_t*, size_t) override { return true; }
    void clearBlob() override {}
};
struct Gateway
{
    int listener = -1, peer = -1;
    uint16_t port = 0;
    bool active = false, escape = false;
    std::vector<uint8_t> frame;
    std::vector<std::vector<uint8_t>> received;
    Gateway()
    {
        listener = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        assert(listener >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && listen(listener, 1) == 0);
        socklen_t size = sizeof(address);
        assert(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0);
        port = ntohs(address.sin_port);
    }
    ~Gateway()
    {
        if (peer >= 0) close(peer);
        close(listener);
    }
    void disconnect()
    {
        if (peer >= 0) close(peer);
        peer = -1;
        frame.clear();
        active = escape = false;
    }
    void poll()
    {
        if (peer < 0) peer = accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (peer < 0)
        {
            assert(errno == EAGAIN || errno == EWOULDBLOCK);
            return;
        }
        uint8_t bytes[4096];
        for (;;)
        {
            const auto count = recv(peer, bytes, sizeof(bytes), MSG_DONTWAIT);
            if (count < 0)
            {
                assert(errno == EAGAIN || errno == EWOULDBLOCK);
                break;
            }
            if (!count) break;
            for (ssize_t i = 0; i < count; ++i)
            {
                const auto byte = bytes[i];
                if (byte == 0x7e)
                {
                    if (active && !frame.empty()) received.push_back(frame);
                    active = true;
                    escape = false;
                    frame.clear();
                }
                else if (active)
                {
                    if (escape)
                    {
                        frame.push_back(byte ^ 0x20);
                        escape = false;
                    }
                    else if (byte == 0x7d) escape = true;
                    else frame.push_back(byte);
                    assert(frame.size() <= r::kReticulumMtu);
                }
            }
        }
    }
    void write(const uint8_t* data, size_t size)
    {
        std::vector<uint8_t> wire{0x7e};
        for (size_t i = 0; i < size; ++i)
        {
            if (data[i] == 0x7e || data[i] == 0x7d)
            {
                wire.push_back(0x7d);
                wire.push_back(data[i] ^ 0x20);
            }
            else wire.push_back(data[i]);
        }
        wire.push_back(0x7e);
        assert(peer >= 0 && send(peer, wire.data(), wire.size(), MSG_DONTWAIT | MSG_NOSIGNAL) == ssize_t(wire.size()));
    }
    unsigned requests() const
    {
        unsigned count = 0;
        for (const auto& bytes : received)
        {
            r::ParsedPacket packet;
            assert(r::parsePacket(bytes.data(), bytes.size(), &packet));
            if (packet.packet_type == r::PacketType::LinkRequest) ++count;
        }
        return count;
    }
    unsigned pathRequests() const
    {
        uint8_t destination[16];
        r::computePathRequestDestinationHash(destination);
        unsigned count = 0;
        for (const auto& bytes : received)
        {
            r::ParsedPacket packet;
            assert(r::parsePacket(bytes.data(), bytes.size(), &packet));
            if (packet.packet_type == r::PacketType::Data && packet.destination_type == r::DestinationType::Plain &&
                !std::memcmp(packet.destination_hash, destination, 16)) ++count;
        }
        return count;
    }
};
void exercisePublicWorker(const chat::MeshPeerRecord& record, const Identity& remote, bool known)
{
    sys::EventBus::init(64);
    native::PacketRadioHardware radio;
    radio.driver = "none";
    assert(native::configure_packet_radio(radio));
    auto pcm = std::make_shared<call_audio_test::State>();
    native::NativeCallAudioBinding media(call_audio_test::options(pcm));
    assert(media.snapshot().installed);
    Gateway gateway;
    trailmate::linux_app::LinuxMeshWorker worker(42, nullptr, nullptr, false);
    assert(worker.initialize());
    chat::MeshConfig config;
    config.reticulum_lora_enabled = false;
    config.reticulum_wifi_gateway_enabled = true;
    config.reticulum_interface_policy = chat::ReticulumInterfacePolicy::WifiGatewayOnly;
    config.reticulum_anonymous_peer = false;
    std::strcpy(config.reticulum_wifi_gateway_host, "127.0.0.1");
    config.reticulum_wifi_gateway_port = gateway.port;
    worker.applyProtocolConfig(chat::MeshProtocol::Reticulum, config);
    const auto step = [&]
    {
        worker.tick();
        gateway.poll();
        call::service_ui_runtime();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    };
    const auto until = [&](auto ready)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!ready())
        {
            assert(std::chrono::steady_clock::now() < deadline);
            step();
        }
    };
    until([&]
          { return gateway.peer >= 0 && worker.getCapabilities().supports_reticulum_audio_call; });
    if (known) assert(platform::ui::reticulum_directory::mesh_peer_directory().record(record).succeeded());
    else
    {
        chat::MeshPeerRecord absent;
        assert(platform::ui::reticulum_directory::mesh_peer_directory().find(record.identity, absent).code == chat::MeshPeerDirectoryStatusCode::NotFound);
    }
    assert(worker.startReticulumAudioCall(record.identity.reticulum).ok);
    until([&]
          { return call::snapshot().state == call::State::Outgoing; });
    const auto pending = call::snapshot();
    for (unsigned i = 0; i < 20; ++i) step();
    assert(gateway.requests() == 0 && !pcm->live);
    Announce announce(remote);
    assert(announce.build(Announce::Aspect::Telephony, "Worker discovered", 1700000000, true));
    gateway.write(announce.data(), announce.size());
    until([&]
          { return gateway.requests() == 1; });
    assert(!std::memcmp(call::snapshot().link_id, pending.link_id, 16));
    call::hangup();
    until([&]
          { return !call::modal_active(); });
    worker.shutdown();
    media.shutdown();
    native::shutdown_native_audio();
    native::shutdown_linux_music();
    call::service_ui_runtime();
    sys::Event* event = nullptr;
    while (sys::EventBus::subscribe(&event, 0)) delete event;
    assert(!platform::audio::music_blocked());
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string requested(argv[1]);
    const bool missing_keys = requested.rfind("unknown_", 0) == 0;
    const bool invalid_announce = requested == "unknown_invalid";
    const std::string scenario = invalid_announce ? "resolve" : missing_keys ? requested.substr(8)
                                                                             : requested;
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-pending-call-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    {
        Identity local(root / "local.sqlite"), remote(root / "remote.sqlite");
        assert(local.begin() && remote.begin());
        if (invalid_announce)
        {
            using Links = trailmate::linux_app::LinuxReticulumLinks;
            Links staged(local);
            Links::CallHooks callbacks;
            callbacks.admit = [](void*, const auto&)
            { return true; };
            callbacks.receive = [](void*, const auto&, const uint8_t*, size_t)
            { return true; };
            staged.setCallHooks(callbacks);
            unsigned sent = 0;
            const auto sender = +[](void* context, const uint8_t*, size_t)
            { ++*static_cast<unsigned*>(context); return true; };
            Announce remote_announce(remote);
            uint8_t destination[16], link[16];
            remote_announce.destination(Announce::Aspect::Telephony, destination);
            assert(staged.openCall(destination, remote.publicKey().data(), link, 1, sender, &sent, 1, nullptr, true, local.identityHash().data()) == Links::OpenResult::Invalid);
            assert(staged.openCall(destination, nullptr, link, 1, sender, &sent, 1, nullptr, true, remote.identityHash().data()) == Links::OpenResult::Opening);
            assert(!staged.transmitPendingCall(link, 2, sender, &sent, 1) && sent == 0);
            assert(!staged.transmitPendingCall(link, 2, sender, &sent, 1, nullptr, local.publicKey().data()) && sent == 0);
            assert(staged.transmitPendingCall(link, 2, sender, &sent, 1, nullptr, remote.publicKey().data()) && sent == 1);
            assert(!staged.transmitPendingCall(link, 3, sender, &sent, 1, nullptr, remote.publicKey().data()) && sent == 1);
            assert(staged.closeCall(link, 4, sender, &sent));
        }
        Store store;
        chat::MeshPeerDirectoryCore directory(store);
        assert(directory.beginEmpty().succeeded());
        chat::MeshPeerRecord record;
        record.valid = true;
        record.identity = chat::makeMeshPeerReticulumIdentity(chat::makeReticulumPeerIdentity(remote.destinationHash().data(), remote.identityHash().data()));
        record.reticulum.identity = record.identity.reticulum;
        record.reticulum.has_public_keys = record.reticulum.delivery = true;
        std::memcpy(record.reticulum.enc_pub, remote.publicKey().data(), 32);
        std::memcpy(record.reticulum.sig_pub, remote.publicKey().data() + 32, 32);
        if (requested == "worker_known" || requested == "worker_unknown")
        {
            exercisePublicWorker(record, remote, requested == "worker_known");
            std::filesystem::remove_all(root);
            return 0;
        }
        if (!missing_keys) assert(directory.record(record).succeeded());
        auto pcm = std::make_shared<call_audio_test::State>();
        native::NativeCallAudioBinding media(call_audio_test::options(pcm));
        assert(media.snapshot().installed);
        Gateway gateway;
        bool transmit = true;
        Network::Hooks hooks;
        hooks.context = &transmit;
        hooks.transmit_allowed = [](void* context)
        { return *static_cast<bool*>(context); };
        Network network(local, directory, hooks);
        r::ReticulumNetworkConfig config;
        config.interface_count = 1;
        auto& endpoint = config.interfaces[0];
        endpoint.type = r::NetworkInterfaceType::TcpClient;
        endpoint.enabled = true;
        std::strcpy(endpoint.id, "pending-fixture");
        std::strcpy(endpoint.target_host, "127.0.0.1");
        endpoint.target_port = gateway.port;
        assert(network.configure(config, false, "Pending caller"));
        uint32_t now = 1;
        const auto step = [&]
        {
            network.tick(++now, 1700000000);
            gateway.poll();
            call::service_ui_runtime();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        };
        const auto until = [&](auto ready)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            while (!ready())
            {
                assert(std::chrono::steady_clock::now() < deadline);
                step();
            }
        };
        until([&]
              { return network.canCall() && gateway.peer >= 0; });
        if (missing_keys)
        {
            auto no_identity = record.identity.reticulum;
            std::memset(no_identity.identity_hash, 0, 16);
            assert(network.dial(no_identity).failure == chat::MeshOperationFailure::PeerKeyMissing);
            auto mismatch = record.identity.reticulum;
            mismatch.identity_hash[0] ^= 1;
            assert(network.dial(mismatch).failure == chat::MeshOperationFailure::InvalidInput);
            assert(call::snapshot().state == call::State::Idle);
        }
        assert(network.dial(record.identity.reticulum).ok);
        const auto pending = call::snapshot();
        assert(pending.state == call::State::Outgoing && !pending.link_active && !pending.media_active);
        assert(network.dial(record.identity.reticulum).failure == chat::MeshOperationFailure::Busy);
        for (unsigned i = 0; i < 10; ++i) step();
        assert(gateway.requests() == 0 && !pcm->live);
        if (scenario == "resolve" || scenario == "multihop")
        {
            assert(gateway.pathRequests() == 1);
            now += 20000;
            until([&]
                  { return gateway.pathRequests() == 2; });
            assert(call::snapshot().state == call::State::Outgoing && gateway.requests() == 0);
        }
        if (scenario == "cancel")
        {
            call::hangup();
            step();
        }
        else if (scenario == "timeout")
        {
            now += 30001;
            step();
        }
        else if (scenario == "ignored")
        {
            chat::MeshPeerUserFlags flags;
            flags.ignored = true;
            assert(directory.setUserFlags(record.identity, flags).succeeded());
            step();
        }
        else if (scenario == "reset")
        {
            network.clear();
            step();
            gateway.disconnect();
            assert(network.configure(config, false, "Pending caller"));
            until([&]
                  { return network.canCall() && gateway.peer >= 0; });
        }
        else if (scenario == "revoke")
        {
            transmit = false;
            step();
            gateway.disconnect();
            transmit = true;
            until([&]
                  { return network.canCall() && gateway.peer >= 0; });
        }
        else assert(scenario == "resolve" || scenario == "multihop");
        Announce announce(remote);
        assert(announce.build(Announce::Aspect::Telephony, "Delayed peer", 1700000000, true));
        if (invalid_announce)
        {
            std::vector<uint8_t> forged(announce.data(), announce.data() + announce.size());
            forged.back() ^= 1;
            gateway.write(forged.data(), forged.size());
            for (unsigned i = 0; i < 20; ++i) step();
            chat::MeshPeerRecord absent;
            assert(!directory.find(record.identity, absent).succeeded() && gateway.requests() == 0 && call::snapshot().state == call::State::Outgoing);
        }
        uint8_t routed[r::kReticulumMtu], next_hop[16]{77};
        size_t routed_size = sizeof(routed);
        if (scenario == "multihop")
        {
            r::ParsedPacket packet;
            assert(r::parsePacket(announce.data(), announce.size(), &packet));
            assert(r::buildHeader2Packet(r::PacketType::Announce, r::DestinationType::Single,
                                         r::PacketContext::PathResponse, false, next_hop, packet.destination_hash,
                                         packet.payload, packet.payload_len, routed, &routed_size));
            routed[1] = 1;
            gateway.write(routed, routed_size);
        }
        else gateway.write(announce.data(), announce.size());
        if (scenario == "resolve" || scenario == "multihop")
        {
            until([&]
                  { return gateway.requests() == 1; });
            assert(call::snapshot().state == call::State::Outgoing && !std::memcmp(call::snapshot().link_id, pending.link_id, 16));
            for (const auto& bytes : gateway.received)
            {
                r::ParsedPacket packet;
                assert(r::parsePacket(bytes.data(), bytes.size(), &packet));
                if (packet.packet_type != r::PacketType::LinkRequest) continue;
                uint8_t id[16];
                assert(r::computeLinkRequestId(bytes.data(), bytes.size(), id) && !std::memcmp(id, pending.link_id, 16));
                if (scenario == "multihop") assert(packet.transport_id && !std::memcmp(packet.transport_id, next_hop, 16));
            }
        }
        else
        {
            for (unsigned i = 0; i < 30; ++i) step();
            assert(gateway.requests() == 0 && call::snapshot().state != call::State::Outgoing && !pcm->live);
        }
        network.clear();
        media.shutdown();
        native::shutdown_native_audio();
        native::shutdown_linux_music();
        call::service_ui_runtime();
        assert(!platform::audio::music_blocked());
    }
    std::filesystem::remove_all(root);
}
