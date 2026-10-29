#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"
#include "chat/linux_meshcore_group_wire.h"
#include "chat/linux_raw_lora_mesh_adapter.h"
#include "platform/linux/mesh/linux_sqlite_mesh_identity_store.h"
#include "sys/event_bus.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <thread>
#include <unistd.h>

using namespace trailmate::linux_app;
namespace wire = meshcore_wire;
namespace radio = platform::linux_runtime;

struct FakeRadio final : radio::IPacketRadio
{
    radio::RadioSettings settings;
    std::deque<radio::RadioPacket> incoming;
    radio::RadioPacket sent;
    bool online = false;
    unsigned transmissions = 0;
    const char* name() const override { return "test port"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override
    {
        online = true;
        return true;
    }
    void release() override { online = false; }
    bool configureLoRa(const radio::RadioSettings& value) override
    {
        settings = value;
        return true;
    }
    bool startReceive() override { return online; }
    bool transmit(const uint8_t* bytes, std::size_t size) override
    {
        assert(size <= sent.data.size());
        sent.size = size;
        std::memcpy(sent.data.data(), bytes, size);
        ++transmissions;
        return online;
    }
    bool pollReceive(radio::RadioPacket* packet) override
    {
        if (incoming.empty()) return false;
        *packet = incoming.front();
        incoming.pop_front();
        return true;
    }
    float readRssi() override { return -100; }
    bool isOnline() const override { return online; }
    const char* lastError() const override { return "test failure"; }
    radio::RadioSettings appliedLoRaConfig() const override { return settings; }
    radio::RadioStatistics stats() const override { return {}; }
};
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-mc-group-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    chat::MeshConfig config;
    for (unsigned index = 0; index < 16; ++index) config.meshcore_channels[0].key[index] = index;
    wire::Frame frame;
    wire::Incoming decoded;
    // Independently generated AES-ECB/HMAC-SHA256 fixtures, not this codec's encoder.
    const char* golden[] = {"1500BE746A6F4CD938347DEF6362894D25A578372E",
                            "5540BE45746A7A166F4CD938347DEF6362894D25A578372E"};
    for (unsigned version = 0; version < 2; ++version)
    {
        config.meshcore_send_profile = version ? chat::MeshCorePayloadSendProfile::V2Only : chat::MeshCorePayloadSendProfile::V1Only;
        assert(wire::encodeText(config, chat::ChannelId::PRIMARY, "peer: hi", 0x12345678, frame));
        assert(chat::meshcore::toHex(frame.bytes.data(), frame.size) == golden[version]);
        assert(wire::decode(config, frame.bytes.data(), frame.size, decoded));
        assert(decoded.text && decoded.sender == 0 && decoded.timestamp == 0x12345678);
        assert(std::string(reinterpret_cast<char*>(decoded.payload.data()), decoded.size) == "peer: hi");
        auto corrupt = frame;
        corrupt.bytes[corrupt.size - 1] ^= 1;
        assert(!wire::decode(config, corrupt.bytes.data(), corrupt.size, decoded));
        auto other_key = config;
        other_key.meshcore_channels[0].key[0] ^= 1;
        assert(!wire::decode(other_key, frame.bytes.data(), frame.size, decoded));
        auto disabled = config;
        disabled.meshcore_channels[0].enabled = false;
        assert(!wire::decode(disabled, frame.bytes.data(), frame.size, decoded));
    }
    assert(!wire::encodeText(config, chat::ChannelId::PRIMARY, std::string(400, 'x'), 1, frame));
    assert(!wire::encodeText(config, chat::ChannelId::TEAM, "invalid", 1, frame));
    assert(!wire::encodeText(config, chat::ChannelId::PRIMARY, std::string("x\0y", 3), 1, frame));
    const uint8_t body[] = {0x10, 0x00, 0x20};
    const uint8_t ambiguous[] = {0x10, 0x00};
    assert(!wire::encodeData(config, chat::ChannelId::PRIMARY, 99, 123, ambiguous, sizeof(ambiguous), frame));
    assert(wire::encodeData(config, chat::ChannelId::PRIMARY, 99, 123, body, sizeof(body), frame));
    assert(wire::decode(config, frame.bytes.data(), frame.size, decoded));
    assert(!decoded.text && decoded.sender == 99 && decoded.port == 123 && decoded.size == sizeof(body));
    assert(!std::memcmp(decoded.payload.data(), body, sizeof(body)));
    // Fuzz every frame length/type without device I/O. Decoder must stay bounded.
    std::array<uint8_t, 256> malformed{};
    for (unsigned type = 0; type < 256; ++type)
        for (unsigned size = 0; size <= 256; ++size)
        {
            malformed[0] = type;
            (void)wire::decode(config, malformed.data(), size, decoded);
        }
    uint32_t persisted_peer = 0;
    {
        FakeRadio tx, rx;
        LinuxRawLoraMeshAdapter sender(tx, 11), receiver(rx, 22);
        setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "sender").c_str(), 1);
        sender.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
        setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "receiver").c_str(), 1);
        receiver.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
        assert(sender.getNodeId() && receiver.getNodeId() && sender.getNodeId() != receiver.getNodeId());
        persisted_peer = receiver.getNodeId();
        sender.setUserInfo("Peer", "P");
        chat::MessageId id = 0;
        assert(sender.sendText(chat::ChannelId::PRIMARY, "hello", &id));
        assert(id && tx.transmissions == 1 && tx.sent.data[0] == 0x55);
        assert(tx.settings.sync_word == 0x12);
        assert(tx.settings.sf == config.meshcore_sf && tx.settings.bw_khz == config.meshcore_bw_khz);
        rx.incoming.push_back(tx.sent);
        rx.incoming.push_back(tx.sent);
        chat::MeshIncomingText text;
        assert(receiver.pollIncomingText(&text));
        assert(text.text == "P: hello" && text.source_unverified && text.from == 0 && text.encrypted);
        assert(!receiver.pollIncomingText(&text));
        tx.incoming.push_back(tx.sent);
        assert(!sender.pollIncomingText(&text)); // Ignore our own relayed group text.
        assert(!sender.sendText(chat::ChannelId::PRIMARY, "direct", &id, 22));
        assert(sender.getCapabilities().supports_unicast_text);
        assert(sender.sendTextDetailed(chat::ChannelId::PRIMARY, "missing", 0, 22).failure == chat::MeshOperationFailure::PeerKeyMissing);
        assert(!sender.isPkiReady());
        assert(sender.sendAppData(chat::ChannelId::PRIMARY, 123, body, sizeof(body)));
        rx.incoming.push_back(tx.sent);
        chat::MeshIncomingData data;
        assert(receiver.pollIncomingData(&data));
        assert(data.from == sender.getNodeId() && data.portnum == 123 && data.payload.size() == sizeof(body));
        assert(sender.triggerDiscoveryActionDetailed(chat::MeshDiscoveryAction::SendIdLocal).ok);
        assert((tx.sent.data[0] & 3) == 2);
        const auto announcement = tx.sent;
        auto tampered = announcement;
        tampered.data[tampered.size - 1] ^= 1;
        rx.incoming.push_back(tampered);
        receiver.tick();
        sys::Event* event = nullptr;
        assert(!sys::EventBus::subscribe(&event));
        rx.incoming.push_back(announcement);
        rx.incoming.push_back(announcement);
        receiver.tick();
        assert(sys::EventBus::subscribe(&event));
        assert(event->type == sys::EventType::NodeInfoUpdate);
        const auto* update = static_cast<const sys::NodeInfoUpdateEvent*>(event);
        assert(update->node_id == sender.getNodeId());
        assert(update->protocol == static_cast<uint8_t>(chat::MeshProtocol::MeshCore));
        assert(update->has_public_key && !update->key_manually_verified);
        assert(std::string(update->short_name) == "P");
        delete event;
        assert(!sys::EventBus::subscribe(&event));
        // Exchange a signed advert in the reverse direction before private messaging.
        assert(receiver.triggerDiscoveryActionDetailed(chat::MeshDiscoveryAction::SendIdLocal).ok);
        setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "sender").c_str(), 1);
        tx.incoming.push_back(rx.sent);
        sender.tick();
        assert(sys::EventBus::subscribe(&event));
        delete event;
        const auto sent = sender.sendTextDetailed(chat::ChannelId::PRIMARY, "private hello", 0, receiver.getNodeId());
        assert(sent.ok);
        setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "receiver").c_str(), 1);
        rx.incoming.push_back(tx.sent);
        rx.incoming.push_back(tx.sent);
        assert(receiver.pollIncomingText(&text));
        assert(text.from == sender.getNodeId() && text.to == receiver.getNodeId());
        assert(text.text == "private hello" && text.encrypted && !text.source_unverified);
        assert(!receiver.pollIncomingText(&text));
        const auto reply_count = rx.transmissions;
        std::this_thread::sleep_for(std::chrono::milliseconds(140));
        receiver.tick();
        assert(rx.transmissions == reply_count + 1);
        setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "sender").c_str(), 1);
        tx.incoming.push_back(rx.sent);
        tx.incoming.push_back(rx.sent);
        sender.tick();
        assert(sys::EventBus::subscribe(&event) && event->type == sys::EventType::ChatSendResult);
        auto* delivered = static_cast<sys::ChatSendResultEvent*>(event);
        assert(delivered->msg_id == sent.msg_id && delivered->status == chat::MessageStatus::Delivered);
        delete event;
        assert(!sys::EventBus::subscribe(&event));
        chat::MessageId pending_id;
        bool pending_ok;
        while (sender.takePendingSendResult(pending_id, pending_ok)) assert(pending_id != sent.msg_id);
        assert(sender.sendAppData(chat::ChannelId::PRIMARY, 123, body, sizeof(body), receiver.getNodeId(), true));
        assert((tx.sent.data[0] & 3) == 2); // Authenticated PATH taught the direct route.
        rx.incoming.push_back(tx.sent);
        assert(receiver.pollIncomingData(&data));
        assert(data.to == receiver.getNodeId() && data.from == sender.getNodeId() && data.portnum == 123);
        assert(data.payload.size() == sizeof(body) && !std::memcmp(data.payload.data(), body, sizeof(body)));
        std::this_thread::sleep_for(std::chrono::milliseconds(140));
        receiver.tick();
        tx.incoming.push_back(rx.sent);
        sender.tick();
        assert(!sys::EventBus::subscribe(&event));
        const auto cancelled = sender.sendTextDetailed(chat::ChannelId::PRIMARY, "cancel on retune", 0, receiver.getNodeId());
        assert(cancelled.ok);
        auto retuned = config;
        ++retuned.meshcore_sf;
        sender.applyProtocolConfig(chat::MeshProtocol::MeshCore, retuned);
        assert(tx.settings.sf == retuned.meshcore_sf);
        assert(sys::EventBus::subscribe(&event) && event->type == sys::EventType::ChatSendResult);
        assert(static_cast<sys::ChatSendResultEvent*>(event)->msg_id == cancelled.msg_id);
        assert(static_cast<sys::ChatSendResultEvent*>(event)->status == chat::MessageStatus::Failed);
        delete event;
        assert(!sys::EventBus::subscribe(&event));
        assert(!sender.takePendingSendResult(pending_id, pending_ok));
        sender.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
        assert(sender.sendAppData(chat::ChannelId::PRIMARY, 123, body, sizeof(body), receiver.getNodeId(), false));
        assert((tx.sent.data[0] & 3) == 1); // Retuning invalidates learned paths.
        const auto mc_id = sender.getNodeId();
        assert(sender.triggerDiscoveryActionDetailed(chat::MeshDiscoveryAction::ScanLocal).ok);
        const auto scan_tx_count = tx.transmissions;
        assert(sender.triggerDiscoveryActionDetailed(chat::MeshDiscoveryAction::ScanLocal).failure == chat::MeshOperationFailure::Busy);
        assert(sender.triggerDiscoveryActionDetailed(chat::MeshDiscoveryAction::SendIdLocal).failure == chat::MeshOperationFailure::Busy);
        assert(!sender.sendText(chat::ChannelId::PRIMARY, "guarded", &id));
        assert(tx.transmissions == scan_tx_count);
        chat::meshcore::ParsedPacket parsed_scan;
        chat::meshcore::DecodedDiscoverRequest scan;
        assert(chat::meshcore::parsePacket(tx.sent.data.data(), tx.sent.size, &parsed_scan));
        assert(chat::meshcore::decodeDiscoverRequest(parsed_scan.payload, parsed_scan.payload_len, &scan));
        uint8_t unsigned_key[32];
        std::memset(unsigned_key, 0x33, sizeof(unsigned_key));
        uint8_t control[38]{};
        std::size_t control_size = 0;
        assert(chat::meshcore::buildDiscoverResponseControlPayload(1, -3, scan.tag, unsigned_key,
                                                                   sizeof(unsigned_key), control, sizeof(control), &control_size));
        radio::RadioPacket scan_reply;
        assert(chat::meshcore::buildFrameNoTransport(chat::meshcore::PayloadProfile::V2, 2,
                                                     chat::meshcore::kMeshCorePayloadTypeControl, nullptr, 0, control, control_size,
                                                     scan_reply.data.data(), scan_reply.data.size(), &scan_reply.size));
        tx.incoming.push_back(scan_reply);
        tx.incoming.push_back(scan_reply);
        sender.tick();
        assert(sys::EventBus::subscribe(&event) && event->type == sys::EventType::NodeInfoUpdate);
        update = static_cast<const sys::NodeInfoUpdateEvent*>(event);
        assert(update->node_id == 0x33333333 && !update->has_public_key && !update->has_public_key_state);
        delete event;
        assert(!sys::EventBus::subscribe(&event));
        sender.applyProtocolConfig(chat::MeshProtocol::Meshtastic, config);
        assert(sender.getNodeId() == 11);
        sender.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
        assert(sender.getNodeId() == mc_id);
        auto no_tx = config;
        no_tx.tx_enabled = false;
        sender.applyProtocolConfig(chat::MeshProtocol::MeshCore, no_tx);
        assert(!sender.sendText(chat::ChannelId::PRIMARY, "disabled", &id));
        sender.applyProtocolConfig(chat::MeshProtocol::Reticulum, config);
        const auto count = tx.transmissions;
        assert(!sender.sendText(chat::ChannelId::PRIMARY, "no legacy fallback", &id));
        assert(tx.transmissions == count);
    }
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "receiver").c_str(), 1);
    platform::linux_runtime::LinuxMeshCoreIdentity peer_identity;
    assert(peer_identity.begin() && peer_identity.nodeId() == persisted_peer);
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "sender").c_str(), 1);
    mesh::PeerPublicKey corrupt_alias;
    corrupt_alias.node_id = mesh::NodeId{persisted_peer ^ 0x10000U};
    std::memcpy(corrupt_alias.public_key, peer_identity.publicKey().data(), 32);
    platform::linux_runtime::mesh::LinuxSqlitePeerKeyStore peer_store({"meshcore", "peer_keys", 64});
    assert(peer_store.put(corrupt_alias).ok);
    {
        FakeRadio radio;
        LinuxRawLoraMeshAdapter reloaded(radio, 11);
        reloaded.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
        assert(reloaded.sendTextDetailed(chat::ChannelId::PRIMARY, "after restart", 0, persisted_peer).ok);
        assert(reloaded.sendTextDetailed(chat::ChannelId::PRIMARY, "wrong binding", 0,
                                         corrupt_alias.node_id.value)
                   .failure == chat::MeshOperationFailure::PeerKeyMissing);
    }
    std::filesystem::remove_all(root);
}
