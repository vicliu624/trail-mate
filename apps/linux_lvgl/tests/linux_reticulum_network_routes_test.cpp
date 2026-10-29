#include "chat/infra/mesh_peer_directory_core.h"
#include "chat/linux_reticulum_network.h"
#include "chat/ports/track_delivery_binding.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace r = chat::reticulum;
using Identity = platform::linux_runtime::LinuxReticulumIdentity;
using Links = trailmate::linux_app::LinuxReticulumLinks;
using Network = trailmate::linux_app::LinuxReticulumNetwork;
struct Store : chat::IMeshPeerDirectoryBlobStore
{
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(std::vector<uint8_t>&) override { return chat::MeshPeerDirectoryBlobLoadResult::Missing; }
    bool saveBlob(const uint8_t*, size_t) override { return true; }
    void clearBlob() override {}
};
struct Socket
{
    int listener = -1, peer = -1;
    uint16_t port = 0;
    bool active = false, escape = false;
    std::vector<uint8_t> buffer;
    unsigned frames = 0;
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
    ~Socket()
    {
        if (peer >= 0) close(peer);
        close(listener);
    }
    void acceptPending()
    {
        if (peer >= 0) return;
        peer = accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (peer < 0) assert(errno == EAGAIN || errno == EWOULDBLOCK);
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
    void write(const uint8_t* data, size_t size)
    {
        assert(peer >= 0);
        std::vector<uint8_t> wire{0x7e};
        for (size_t index = 0; index < size; ++index)
        {
            if (data[index] == 0x7d || data[index] == 0x7e)
            {
                wire.push_back(0x7d);
                wire.push_back(data[index] ^ 0x20);
            }
            else wire.push_back(data[index]);
        }
        wire.push_back(0x7e);
        assert(send(peer, wire.data(), wire.size(), MSG_DONTWAIT | MSG_NOSIGNAL) == ssize_t(wire.size()));
    }
    template <class Receiver>
    void poll(Receiver receiver)
    {
        if (peer < 0) return;
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
            for (ssize_t index = 0; index < count; ++index)
            {
                const auto value = bytes[index];
                if (value == 0x7e)
                {
                    if (active && !buffer.empty())
                    {
                        ++frames;
                        receiver(buffer.data(), buffer.size());
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
                    assert(buffer.size() <= 564);
                }
            }
        }
    }
};
struct Outcome
{
    chat::MessageId id;
    chat::MessageStatus status;
    chat::delivery::SendFailureKind failure;
};
int main()
{
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("trailmate-network-routes-" + std::to_string(getpid()));
    fs::create_directories(root);
    {
        Identity local(root / "local.sqlite"), remote(root / "remote.sqlite");
        assert(local.begin() && remote.begin());
        Store store;
        chat::MeshPeerDirectoryCore directory(store);
        assert(directory.beginEmpty().succeeded());
        struct Context
        {
            bool current = true, transmit = true;
            std::vector<chat::MeshIncomingText> incoming;
            std::vector<Outcome> outcomes;
            std::vector<uint8_t> custom;
            unsigned custom_calls = 0;
        } context;
        Network::Hooks hooks;
        hooks.context = &context;
        hooks.context_current = [](void* value)
        { return static_cast<Context*>(value)->current; };
        hooks.transmit_allowed = [](void* value)
        { return static_cast<Context*>(value)->transmit; };
        hooks.incoming = [](void* value, const chat::MeshIncomingText& message)
        { static_cast<Context*>(value)->incoming.push_back(message); return true; };
        hooks.outcome = [](void* value, chat::MessageId id, chat::MessageStatus status, chat::delivery::SendFailureKind failure)
        { static_cast<Context*>(value)->outcomes.push_back({id, status, failure}); };
        hooks.custom = [](void* value, chat::lxmf::ByteSpan type, const chat::lxmf::CustomDeliveryView& view)
        {
            if (!chat::lxmf::isTrackCustomType(type)) return false;
            auto& context = *static_cast<Context*>(value);
            context.custom.assign(view.data.data, view.data.data + view.data.size);
            ++context.custom_calls;
            return true;
        };
        hooks.custom_outcome = hooks.outcome;
        Network network(local, directory, hooks);
        Socket a, b;
        r::ReticulumNetworkConfig config;
        config.interface_count = 2;
        for (unsigned index = 0; index < 2; ++index)
        {
            auto& endpoint = config.interfaces[index];
            endpoint.type = r::NetworkInterfaceType::TcpClient;
            endpoint.enabled = true;
            std::snprintf(endpoint.id, sizeof(endpoint.id), "%s", index ? "second" : "first");
            std::snprintf(endpoint.target_host, sizeof(endpoint.target_host), "127.0.0.1");
            endpoint.target_port = index ? b.port : a.port;
        }
        assert(network.configure(config, false, "Local route fixture"));
        struct PeerContext
        {
            Socket& socket;
            std::vector<std::vector<uint8_t>> sent;
            std::vector<chat::MeshIncomingText> incoming;
            std::vector<Outcome> outcomes;
            const Identity& local;
            std::vector<uint8_t> custom;
            unsigned custom_calls = 0;
            static bool send(void* pointer, const uint8_t* data, size_t size)
            {
                auto& self = *static_cast<PeerContext*>(pointer);
                self.sent.emplace_back(data, data + size);
                self.socket.write(data, size);
                return true;
            }
        } remote_context{a, {}, {}, {}, local};
        Links peer(remote);
        peer.setIncomingHooks([](void* pointer, const chat::MeshIncomingText& message)
                              { static_cast<PeerContext*>(pointer)->incoming.push_back(message); return true; },
                              [](void* pointer, const uint8_t*, uint8_t* key, bool* ignored)
                              { *ignored = false; std::memcpy(key, static_cast<PeerContext*>(pointer)->local.publicKey().data(), 64); return true; },
                              &remote_context);
        peer.setCustomReceiver([](void* pointer, chat::lxmf::ByteSpan type, const chat::lxmf::CustomDeliveryView& view)
                               {
            if (!chat::lxmf::isTrackCustomType(type)) return false;
            auto& peer = *static_cast<PeerContext*>(pointer);
            peer.custom.assign(view.data.data, view.data.data + view.data.size); ++peer.custom_calls;
            return true; },
                               &remote_context);
        peer.setOutcomeSink([](void* pointer, chat::MessageId id, chat::MessageStatus status, chat::delivery::SendFailureKind failure)
                            { static_cast<PeerContext*>(pointer)->outcomes.push_back({id, status, failure}); },
                            &remote_context);
        peer.setCustomOutcomeSink([](void* pointer, chat::MessageId id, chat::MessageStatus status, chat::delivery::SendFailureKind failure)
                                  { static_cast<PeerContext*>(pointer)->outcomes.push_back({id, status, failure}); }, &remote_context);
        uint32_t now = 1;
        const auto step = [&]
        {
            now += 2;
            network.tick(now, 1700000000);
            a.acceptPending();
            b.acceptPending();
            a.poll([&](const uint8_t* data, size_t size)
                   { (void)peer.receive(data, size, now, PeerContext::send, &remote_context); });
            b.poll([](const uint8_t*, size_t) {});
            peer.processOutbound(now, PeerContext::send, &remote_context);
            peer.retryAccepted(now, PeerContext::send, &remote_context);
            peer.tick(now);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
              { return network.ready() && a.peer >= 0 && b.peer >= 0 && b.frames > 0; });
        assert(peer.queueText(local.destinationHash().data(), local.publicKey().data(), "first", 1, 1700000000, now, PeerContext::send, &remote_context) == Links::QueueResult::Queued);
        until([&]
              { return context.incoming.size() == 1; });
        network.commitIncoming(context.incoming[0], true);
        until([&]
              { return std::any_of(remote_context.outcomes.begin(), remote_context.outcomes.end(), [](const auto& result)
                                   { return result.id == 1 && result.status == chat::MessageStatus::Delivered; }); });
        std::vector<uint8_t> replay;
        for (const auto& packet : remote_context.sent)
        {
            r::ParsedPacket parsed;
            assert(r::parsePacket(packet.data(), packet.size(), &parsed));
            if (parsed.packet_type == r::PacketType::Data && parsed.context == 0) replay = packet;
        }
        assert(!replay.empty());
        const auto rejected = network.snapshot().rejected_routes;
        b.write(replay.data(), replay.size());
        until([&]
              { return network.snapshot().rejected_routes > rejected; });
        assert(context.incoming.size() == 1 && network.snapshot().links.active == 1);
        context.current = false;
        assert(peer.queueText(local.destinationHash().data(), local.publicKey().data(), "during-update", 2, 1700000000, now, PeerContext::send, &remote_context) == Links::QueueResult::Queued);
        for (unsigned index = 0; index < 20; ++index) step();
        assert(context.incoming.size() == 1 && network.snapshot().links.active == 1);
        context.current = true;
        until([&]
              { return context.incoming.size() == 2; });
        network.commitIncoming(context.incoming[1], true);
        b.disconnect();
        until([&]
              { return network.snapshot().tcp.endpoints[1].state != trailmate::linux_app::LinuxReticulumTcp::State::Connected; });
        assert(network.ready() && network.snapshot().links.active == 1);
        const auto before_b = b.frames;
        const auto target = chat::makeReticulumPeerIdentity(remote.destinationHash().data(), remote.identityHash().data());
        assert(network.hasRoute(target.destination_hash));
        assert(network.queueText(target, "reply", 3, 1700000000) == Links::QueueResult::Queued);
        until([&]
              { return remote_context.incoming.size() == 1; });
        assert(remote_context.incoming[0].text == "reply" && b.frames == before_b);
        peer.commitIncoming(remote_context.incoming[0], true, now, PeerContext::send, &remote_context);
        until([&]
              { return std::any_of(context.outcomes.begin(), context.outcomes.end(), [](const auto& result)
                                   { return result.id == 3 && result.status == chat::MessageStatus::Delivered; }); });
        std::vector<uint8_t> custom(8192, 41);
        assert(network.queueCustom(target, "trailmate.track.v1", {custom.data(), custom.size()}, 6, 1700000000) == Links::QueueResult::Queued);
        until([&]
              { return remote_context.custom_calls == 1; });
        assert(remote_context.custom == custom && remote_context.incoming.size() == 1 && b.frames == before_b);
        until([&]
              { return std::any_of(context.outcomes.begin(), context.outcomes.end(), [](const auto& result)
                                   { return result.id == 6 && result.status == chat::MessageStatus::Delivered; }); });
        const uint8_t control[] = {0x91, 1};
        assert(peer.queueCustom(local.destinationHash().data(), local.publicKey().data(), "trailmate.track.v1", {control, sizeof(control)},
                                7, 1700000000, now, PeerContext::send, &remote_context) == Links::QueueResult::Queued);
        until([&]
              { return context.custom_calls == 1; });
        assert(context.custom == std::vector<uint8_t>(control, control + sizeof(control)) && context.incoming.size() == 2);
        until([&]
              { return std::any_of(remote_context.outcomes.begin(), remote_context.outcomes.end(), [](const auto& result)
                                   { return result.id == 7 && result.status == chat::MessageStatus::Delivered; }); });
        assert(network.queueText(target, "unacknowledged", 4, 1700000000) == Links::QueueResult::Queued);
        until([&]
              { return remote_context.incoming.size() == 2; });
        r::ReticulumNetworkConfig empty;
        assert(network.configure(empty, false, "Local route fixture"));
        assert(!network.ready() && network.pendingOutboundIds().empty());
        assert(std::any_of(context.outcomes.begin(), context.outcomes.end(), [](const auto& result)
                           { return result.id == 4 && result.status == chat::MessageStatus::Failed && result.failure == chat::delivery::SendFailureKind::RadioSendFailed; }));
    }
    fs::remove_all(root);
}
