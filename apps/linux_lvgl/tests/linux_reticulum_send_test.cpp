#include "app/linux_app_services.h"
#include "chat/infra/rnode/rnode_packet_wire.h"
#include "chat/linux_reticulum_links.h"
#include "chat/ports/i_mesh_peer_directory.h"
#include "chat/usecase/chat_service.h"
#include "platform/linux/packet_radio.h"
#include "platform/linux/runtime_paths.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "sys/clock.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <sqlite3.h>
#include <thread>
#include <unistd.h>

namespace radio = platform::linux_runtime;
namespace r = chat::reticulum;
using Links = trailmate::linux_app::LinuxReticulumLinks;
using Identity = radio::LinuxReticulumIdentity;
using namespace std::chrono_literals;
struct State
{
    std::mutex mutex;
    std::deque<radio::RadioPacket> rx;
    std::deque<std::vector<uint8_t>> tx;
    bool online = false, fail = false, ui_io = false;
    std::thread::id ui = std::this_thread::get_id();
    radio::RadioSettings settings;
    void io()
    {
        if (std::this_thread::get_id() == ui) ui_io = true;
    }
};
struct Driver final : radio::IPacketRadio
{
    State& s;
    explicit Driver(State& state) : s(state) {}
    const char* name() const override { return "Reticulum test radio"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override
    {
        std::lock_guard lock(s.mutex);
        s.io();
        s.online = true;
        return true;
    }
    void release() override
    {
        std::lock_guard lock(s.mutex);
        s.io();
        s.online = false;
    }
    bool configureLoRa(const radio::RadioSettings& value) override
    {
        std::lock_guard lock(s.mutex);
        s.io();
        s.settings = value;
        return true;
    }
    bool startReceive() override
    {
        std::lock_guard lock(s.mutex);
        s.io();
        return true;
    }
    bool transmit(const uint8_t* data, size_t size) override
    {
        std::lock_guard lock(s.mutex);
        s.io();
        if (s.fail) return false;
        s.tx.emplace_back(data, data + size);
        return true;
    }
    bool pollReceive(radio::RadioPacket* packet) override
    {
        std::lock_guard lock(s.mutex);
        s.io();
        if (s.rx.empty()) return false;
        *packet = s.rx.front();
        s.rx.pop_front();
        return true;
    }
    float readRssi() override
    {
        std::lock_guard lock(s.mutex);
        s.io();
        return -95;
    }
    bool isOnline() const override
    {
        std::lock_guard lock(s.mutex);
        return s.online;
    }
    const char* lastError() const override { return "injected TX failure"; }
    radio::RadioSettings appliedLoRaConfig() const override
    {
        std::lock_guard lock(s.mutex);
        return s.settings;
    }
    radio::RadioStatistics stats() const override
    {
        std::lock_guard lock(s.mutex);
        radio::RadioStatistics out;
        out.online = s.online;
        return out;
    }
};
struct PeerWire
{
    State& state;
    uint8_t sequence = 0;
    bool hold = false;
    std::vector<uint8_t> proof;
    void inject(const uint8_t* data, size_t size)
    {
        chat::rnode::EncodedAirPacketSet encoded;
        assert(chat::rnode::encodeAirPacketSet(data, size, sequence++, &encoded));
        std::lock_guard lock(state.mutex);
        radio::RadioPacket packet;
        packet.size = encoded.first_len;
        std::memcpy(packet.data.data(), encoded.first, packet.size);
        state.rx.push_back(packet);
        if (encoded.count == 2)
        {
            packet.size = encoded.second_len;
            std::memcpy(packet.data.data(), encoded.second, packet.size);
            state.rx.push_back(packet);
        }
    }
    static bool send(void* pointer, const uint8_t* data, size_t size)
    {
        auto& wire = *static_cast<PeerWire*>(pointer);
        r::ParsedPacket packet;
        assert(r::parsePacket(data, size, &packet));
        if (wire.hold && packet.packet_type == r::PacketType::Proof &&
            (packet.context == 0 || packet.context == static_cast<uint8_t>(r::PacketContext::ResourcePrf)))
            wire.proof.assign(data, data + size);
        else wire.inject(data, size);
        return true;
    }
};
std::vector<uint8_t> announce(Identity& identity, uint8_t nonce)
{
    uint8_t name[10], signature[64];
    r::computeNameHash("lxmf", "delivery", name);
    std::vector<uint8_t> payload(identity.publicKey().begin(), identity.publicKey().end());
    payload.insert(payload.end(), name, name + 10);
    payload.insert(payload.end(), {nonce, 0, 0, 0, 0, 0, 0, 0, 0, nonce});
    uint8_t app[64];
    size_t app_size = sizeof(app);
    assert(chat::lxmf::packPeerAnnounceAppData("test peer", false, 0, app, &app_size));
    std::vector<uint8_t> domain(identity.destinationHash().begin(), identity.destinationHash().end());
    domain.insert(domain.end(), payload.begin(), payload.end());
    domain.insert(domain.end(), app, app + app_size);
    assert(identity.sign(domain.data(), domain.size(), signature));
    payload.insert(payload.end(), signature, signature + 64);
    payload.insert(payload.end(), app, app + app_size);
    std::vector<uint8_t> raw(500);
    size_t size = raw.size();
    assert(r::buildHeader1Packet(r::PacketType::Announce, r::DestinationType::Single, r::PacketContext::PathResponse, false,
                                 identity.destinationHash().data(), payload.data(), payload.size(), raw.data(), &size));
    raw.resize(size);
    return raw;
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-reticulum-send-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-test-missing", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    setenv("TRAIL_MATE_DESKTOP_NOTIFICATIONS", "0", 1);
    Identity remote(root / "remote.sqlite");
    assert(remote.begin());
    std::vector<chat::MessageStatus> peer_status;
    Links peer(remote);
    peer.setOutcomeSink([](void* pointer, chat::MessageId, chat::MessageStatus status, chat::delivery::SendFailureKind)
                        { static_cast<decltype(peer_status)*>(pointer)->push_back(status); },
                        &peer_status);
    std::vector<chat::MeshIncomingText> received;
    peer.setIncomingHooks([](void* pointer, const chat::MeshIncomingText& message)
                          {
        static_cast<decltype(received)*>(pointer)->push_back(message); return true; },
                          nullptr, &received);
    State state;
    radio::PacketRadioCoordinator coordinator(std::make_unique<Driver>(state));
    PeerWire wire{state};
    chat::rnode::ReassemblyState assembly;
    unsigned requests = 0, unknown_requests = 0, advertisements = 0;
    {
        trailmate::linux_app::LinuxAppServicesOptions options;
        options.runtime_mode = radio::LinuxRuntimeMode::DeviceRealMesh;
        options.packet_radio = &coordinator.mesh();
        trailmate::linux_app::LinuxAppServices services(options);
        assert(services.initialize() && services.switchMeshProtocol(chat::MeshProtocol::Reticulum, false));
        bool capture = false;
        const auto pump = [&]
        {
            services.dispatchPendingEvents();
            for (;;)
            {
                std::vector<uint8_t> air;
                {
                    std::lock_guard lock(state.mutex);
                    if (state.tx.empty()) break;
                    air = std::move(state.tx.front());
                    state.tx.pop_front();
                }
                if (!capture) continue;
                uint8_t raw[508];
                size_t size = sizeof(raw);
                bool complete = false;
                assert(chat::rnode::feedAirPacket(&assembly, air.data(), air.size(), raw, &size, &complete));
                if (!complete) continue;
                r::ParsedPacket packet;
                assert(r::parsePacket(raw, size, &packet));
                if (packet.destination_type == r::DestinationType::Plain)
                {
                    assert(packet.payload_len == 32);
                    ++requests;
                    if (!std::memcmp(packet.payload, remote.destinationHash().data(), 16))
                    {
                        auto response = announce(remote, static_cast<uint8_t>(requests));
                        wire.inject(response.data(), response.size());
                    }
                    else ++unknown_requests;
                }
                else
                {
                    if (packet.context == static_cast<uint8_t>(r::PacketContext::ResourceAdv)) ++advertisements;
                    peer.receive(raw, size, sys::millis_now(), PeerWire::send, &wire);
                }
            }
            peer.processOutbound(sys::millis_now(), PeerWire::send, &wire);
            peer.retryAccepted(sys::millis_now(), PeerWire::send, &wire);
            peer.tick(sys::millis_now());
        };
        const auto wait = [&](auto predicate)
        {
            const auto end = std::chrono::steady_clock::now() + 8s;
            while (!predicate() && std::chrono::steady_clock::now() < end)
            {
                pump();
                std::this_thread::sleep_for(1ms);
            }
            assert(predicate());
        };
        wait([&]
             { return services.meshAdapter()->isReady(); });
        {
            std::lock_guard lock(state.mutex);
            state.tx.clear();
        }
        assembly.reset();
        capture = true;
        assert(services.meshAdapter()->getCapabilities().supports_reticulum_destination_text);
        chat::ConversationId conversation(chat::ChannelId::PRIMARY, 0, chat::MeshProtocol::Reticulum);
        conversation.reticulum_identity = chat::makeReticulumPeerIdentity(remote.destinationHash().data(), remote.identityHash().data());
        std::string long_text(60000, 'x');
        uint32_t noise = 0x74ad193b;
        for (auto& character : long_text)
        {
            noise ^= noise << 13;
            noise ^= noise >> 17;
            noise ^= noise << 5;
            character = static_cast<char>(32 + noise % 95);
        }
        assert(Links::textFitsTransport(long_text) && !Links::textFitsSinglePacket(long_text));
        const auto sent = services.getChatService().sendTextToConversationDetailed(conversation, long_text);
        assert(sent.ok);
        wait([&]
             { return advertisements > 0; });
        const auto urgent = services.getChatService().sendTextToConversationDetailed(conversation, "short message during resource");
        assert(urgent.ok);
        wait([&]
             { const auto* message = services.getChatService().getMessageForProtocol(sent.msg_id, chat::MeshProtocol::Reticulum);
                   return received.size() == 2 && message && message->status == chat::MessageStatus::Sent; });
        assert(requests && received[0].text == "short message during resource" && received[1].text == long_text);
        peer.commitIncoming(received[0], true, sys::millis_now(), PeerWire::send, &wire);
        wire.hold = true;
        peer.commitIncoming(received[1], true, sys::millis_now(), PeerWire::send, &wire);
        assert(!wire.proof.empty());
        auto bad = wire.proof;
        bad.back() ^= 1;
        wire.inject(bad.data(), bad.size());
        auto wrong_link = wire.proof;
        wrong_link[2] ^= 1;
        wire.inject(wrong_link.data(), wrong_link.size());
        for (unsigned i = 0; i < 30; ++i)
        {
            pump();
            std::this_thread::sleep_for(1ms);
        }
        assert(services.getChatService().getMessageForProtocol(sent.msg_id, chat::MeshProtocol::Reticulum)->status == chat::MessageStatus::Sent);
        wire.inject(wire.proof.data(), wire.proof.size());
        wait([&]
             { return services.getChatService().getMessageForProtocol(sent.msg_id, chat::MeshProtocol::Reticulum)->status == chat::MessageStatus::Delivered; });
        wire.inject(wire.proof.data(), wire.proof.size());
        pump();
        Identity local(radio::sqlite_database_path());
        assert(local.begin());
        sqlite3* blocked = nullptr;
        assert(sqlite3_open(radio::sqlite_database_path().c_str(), &blocked) == SQLITE_OK);
        assert(sqlite3_exec(blocked, "CREATE TRIGGER reject_resource BEFORE INSERT ON chat_messages WHEN NEW.status=0 "
                                     "BEGIN SELECT RAISE(ABORT,'injected resource persistence failure'); END;",
                            nullptr, nullptr, nullptr) == SQLITE_OK);
        const std::string large_reply(4000, 'r');
        assert(peer.queueText(local.destinationHash().data(), local.publicKey().data(), large_reply, 501, 1700000001.5,
                              sys::millis_now(), PeerWire::send, &wire) == Links::QueueResult::Queued);
        peer.processOutbound(sys::millis_now(), PeerWire::send, &wire);
        wait([&]
             { return !peer_status.empty() && peer_status.back() == chat::MessageStatus::Sent; });
        const auto failed_until = std::chrono::steady_clock::now() + 300ms;
        while (std::chrono::steady_clock::now() < failed_until)
        {
            pump();
            std::this_thread::sleep_for(1ms);
        }
        assert(peer_status.back() == chat::MessageStatus::Sent);
        assert(sqlite3_exec(blocked, "DROP TRIGGER reject_resource;", nullptr, nullptr, nullptr) == SQLITE_OK);
        assert(sqlite3_close(blocked) == SQLITE_OK);
        wait([&]
             {
            const auto messages = services.getChatService().getRecentMessages(conversation, 10);
            return !peer_status.empty() && peer_status.back() == chat::MessageStatus::Delivered &&
                std::any_of(messages.begin(), messages.end(), [&](const auto& message) { return message.text == large_reply; }); });
        {
            std::lock_guard lock(state.mutex);
            state.fail = true;
        }
        const auto failed = services.getChatService().sendTextToConversationDetailed(conversation, "physical TX fails");
        assert(failed.ok);
        wait([&]
             { return services.getChatService().getMessageForProtocol(failed.msg_id, chat::MeshProtocol::Reticulum)->status == chat::MessageStatus::Failed; });
        assert(received.size() == 2);
        {
            std::lock_guard lock(state.mutex);
            state.fail = false;
        }
        auto missing = conversation;
        missing.reticulum_identity.destination_hash[0] ^= 1;
        const auto waiting = services.getChatService().sendTextToConversationDetailed(missing, "waiting for path");
        assert(waiting.ok);
        wait([&]
             { return unknown_requests > 0; });
        capture = false;
        assert(services.switchMeshProtocol(chat::MeshProtocol::Meshtastic, false));
        wait([&]
             { const auto* message = services.getChatService().getMessageForProtocol(waiting.msg_id, chat::MeshProtocol::Reticulum);
                   return message && message->status == chat::MessageStatus::Failed; });
    }
    assert(!state.ui_io);
    assert(platform::ui::reticulum_directory::mesh_peer_directory().flush().succeeded());
    std::filesystem::remove_all(root);
}
