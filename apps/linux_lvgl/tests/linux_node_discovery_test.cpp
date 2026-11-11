#include "chat/infra/mesh_peer_directory_core.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/linux_raw_lora_mesh_adapter.h"
#include "chat/runtime/meshcore_runtime.h"
#include "chat/usecase/contact_service.h"
#include "platform/linux/mesh/linux_sqlite_mesh_identity_store.h"
#include "sys/clock.h"
#include "sys/event_bus.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace
{
namespace mc = chat::meshcore;
namespace wire = trailmate::linux_app::meshcore_wire;
namespace port = platform::linux_runtime;
using Adapter = trailmate::linux_app::LinuxRawLoraMeshAdapter;
uint32_t clock_ms = 100;
uint32_t clock_now() { return clock_ms; }
uint32_t epoch_now() { return static_cast<uint32_t>(std::time(nullptr)); }

struct Radio final : port::IPacketRadio
{
    std::deque<port::RadioPacket> incoming;
    std::vector<port::RadioPacket> sent;
    port::RadioSettings settings;
    bool online = false, fail_tx = false;
    const char* name() const override { return "discovery fixture"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override { return online = true; }
    void release() override { online = false; }
    bool configureLoRa(const port::RadioSettings& value) override
    {
        settings = value;
        return true;
    }
    bool startReceive() override { return online; }
    bool transmit(const uint8_t* bytes, size_t size) override
    {
        assert(size <= port::RadioPacket{}.data.size());
        port::RadioPacket packet;
        packet.size = size;
        std::copy_n(bytes, size, packet.data.begin());
        sent.push_back(packet);
        return online && !fail_tx;
    }
    bool pollReceive(port::RadioPacket* packet) override
    {
        if (incoming.empty()) return false;
        *packet = incoming.front();
        incoming.pop_front();
        return true;
    }
    float readRssi() override { return -90; }
    bool isOnline() const override { return online; }
    const char* lastError() const override { return "fixture"; }
    port::RadioSettings appliedLoRaConfig() const override { return settings; }
    port::RadioStatistics stats() const override { return {}; }
};

struct Blob final : chat::IMeshPeerDirectoryBlobStore
{
    std::vector<uint8_t> bytes;
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(std::vector<uint8_t>& out) override
    {
        out = bytes;
        return bytes.empty() ? chat::MeshPeerDirectoryBlobLoadResult::Missing : chat::MeshPeerDirectoryBlobLoadResult::Loaded;
    }
    bool saveBlob(const uint8_t* data, size_t size) override
    {
        bytes.assign(data, data + size);
        return true;
    }
    void clearBlob() override { bytes.clear(); }
};

void useRoot(const std::filesystem::path& root)
{
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
}

void drain(chat::contacts::ContactService* service = nullptr)
{
    sys::Event* event = nullptr;
    while (sys::EventBus::subscribe(&event))
    {
        if (service && event->type == sys::EventType::NodeInfoUpdate)
        {
            const auto& node = *static_cast<sys::NodeInfoUpdateEvent*>(event);
            service->updateNodeInfo(node.node_id, node.short_name, node.long_name,
                                    node.snr, node.rssi, node.event_timestamp, node.protocol,
                                    node.role, node.hops_away, node.hw_model, node.channel);
        }
        delete event;
    }
}

void announcement(const std::filesystem::path& root, chat::MeshProtocol protocol)
{
    useRoot(root);
    clock_ms = 100;
    Radio radio;
    Adapter adapter(radio, 0x12345678, clock_now);
    chat::MeshConfig config;
    adapter.applyProtocolConfig(protocol, config);
    adapter.setUserInfo("Test local node", "TST");
    adapter.tick();
    assert(radio.sent.empty());
    clock_ms += 4999;
    adapter.tick();
    assert(radio.sent.empty());
    radio.fail_tx = true;
    ++clock_ms;
    adapter.tick();
    assert(radio.sent.size() == 1);
    adapter.tick();
    assert(radio.sent.size() == 1); // Failed broadcasts have a bounded retry interval.
    radio.fail_tx = false;
    clock_ms += 30000;
    adapter.tick();
    assert(radio.sent.size() == 2);
    clock_ms += 29999;
    adapter.tick();
    assert(radio.sent.size() == 2);
    clock_ms += 3U * 60U * 60U * 1000U;
    adapter.tick();
    assert(radio.sent.size() == 3);
    adapter.setUserInfo("Renamed local node", "NEW");
    clock_ms += 5000;
    adapter.tick();
    assert(radio.sent.size() == 4);
    config.tx_enabled = false;
    adapter.applyProtocolConfig(protocol, config);
    clock_ms += 3U * 60U * 60U * 1000U;
    adapter.tick();
    assert(radio.sent.size() == 4);
    config.tx_enabled = true;
    adapter.applyProtocolConfig(protocol, config);
    clock_ms += 5000;
    adapter.tick();
    assert(radio.sent.size() == 5);
    drain();
}

void directoryPromotion()
{
    Blob blob;
    chat::MeshPeerDirectoryCore directory(blob);
    chat::contacts::ContactService contacts(directory);
    contacts.begin();
    contacts.setActiveProtocol(chat::MeshProtocol::MeshCore);
    const uint32_t node = 0x42424242;
    contacts.updateNodeInfo(node, "PEER", "Named peer", 4, -90, epoch_now(),
                            static_cast<uint8_t>(chat::MeshProtocol::MeshCore));
    chat::MeshPeerRecord key;
    key.valid = true;
    key.identity = chat::makeMeshPeerNodeIdentity(chat::MeshProtocol::MeshCore, node);
    key.meshcore.node_id_hint = node;
    key.meshcore.has_public_key = true;
    std::fill_n(key.meshcore.public_key, 32, 0x42);
    assert(contacts.recordPeer(key));
    assert(directory.count(chat::MeshProtocol::MeshCore) == 1);
    std::vector<chat::contacts::PeerBrowseEntry> nearby;
    assert(contacts.browsePeers(chat::MeshPeerDirectoryView::Nearby, "", nearby).succeeded());
    assert(nearby.size() == 1 && nearby[0].kind == chat::MeshPeerIdentityKind::PublicKey);
    chat::contacts::PeerDirectoryItem projected;
    assert(contacts.readPeer(nearby.front(), projected).succeeded());
    assert(projected.has_public_key && std::string(projected.long_name) == "Named peer");
    assert(contacts.addContact(node, "My peer"));
    contacts.updateNodeInfo(node, "NEW", "Updated peer", 7, -80, epoch_now(),
                            static_cast<uint8_t>(chat::MeshProtocol::MeshCore));
    assert(directory.count(chat::MeshProtocol::MeshCore) == 1);
    assert(contacts.browsePeers(chat::MeshPeerDirectoryView::Contacts, "", nearby).succeeded() && nearby.size() == 1);
    assert(contacts.readPeer(nearby.front(), projected).succeeded());
    assert(projected.display_name == "My peer" && std::string(projected.long_name) == "Updated peer");
    assert(directory.flush().succeeded());
    chat::MeshPeerDirectoryCore restored(blob);
    assert(restored.begin().succeeded());
    chat::MeshPeerRecord loaded;
    assert(restored.findByNodeId(chat::MeshProtocol::MeshCore, node, loaded).succeeded());
    assert(loaded.identity.kind == chat::MeshPeerIdentityKind::PublicKey && loaded.meshcore.has_public_key);
    auto missing_hint = loaded;
    missing_hint.meshcore.node_id_hint = 0;
    chat::MeshPeerDirectoryCore::encodeBlob(blob.bytes, {missing_hint});
    chat::MeshPeerDirectoryCore old_hint(blob);
    assert(old_hint.begin().succeeded());
    assert(old_hint.findByNodeId(chat::MeshProtocol::MeshCore, node, missing_hint).succeeded());
    assert(missing_hint.meshcore.node_id_hint == node);

    // An older cache can contain both identities; refreshing it must retain
    // the alias and key trust while removing the anonymous duplicate.
    auto placeholder = loaded;
    placeholder.identity = chat::makeMeshPeerNodeIdentity(chat::MeshProtocol::MeshCore, node);
    placeholder.meshcore.has_public_key = false;
    placeholder.user_alias[0] = '\0';
    loaded.meshcore.public_key_verified = true;
    chat::MeshPeerDirectoryCore::encodeBlob(blob.bytes, {placeholder, loaded});
    chat::MeshPeerDirectoryCore legacy(blob);
    assert(legacy.begin().succeeded() && legacy.count(chat::MeshProtocol::MeshCore) == 2);
    assert(legacy.record(key).succeeded() && legacy.count(chat::MeshProtocol::MeshCore) == 1);
    assert(legacy.findByNodeId(chat::MeshProtocol::MeshCore, node, loaded).succeeded());
    assert(std::string(loaded.user_alias) == "My peer" && loaded.meshcore.public_key_verified);
    auto collision = key;
    collision.meshcore.public_key[31] ^= 1;
    assert(legacy.record(collision).succeeded());
    assert(legacy.count(chat::MeshProtocol::MeshCore) == 2); // Full keys never merge on a short/node-ID collision.
}

void samePrefixDiscovery()
{
    chat::runtime::MeshCoreRuntime runtime;
    chat::runtime::RuntimeContext context;
    context.protocol = chat::MeshProtocol::MeshCore;
    context.self_node = 0x11111142;
    std::array<uint8_t, 32> key;
    key.fill(0x42); // Same one-byte routing hash, different node identity.
    uint8_t payload[38]{};
    size_t size = 0;
    assert(mc::buildDiscoverResponseControlPayload(mc::kMeshCoreAdvertTypeChat, 0, 123,
                                                   key.data(), key.size(), payload, sizeof(payload), &size));
    chat::runtime::IncomingPacket packet;
    packet.protocol = chat::MeshProtocol::MeshCore;
    packet.payload_type = mc::kMeshCorePayloadTypeControl;
    assert(packet.payload.assign(payload, size));
    chat::runtime::ProtocolEffects effects;
    runtime.handleIncomingPacket(packet, context, effects);
    assert(effects.items.size() == 2);
    context.self_node = 0x42424242;
    effects.clear();
    runtime.handleIncomingPacket(packet, context, effects);
    assert(effects.items.empty());
}

void exchange(const std::filesystem::path& root, chat::MeshProtocol protocol,
              chat::MeshCorePayloadSendProfile profile = chat::MeshCorePayloadSendProfile::V2Only)
{
    clock_ms = 100;
    Radio ar, br;
    Adapter a(ar, 0x11112222, clock_now), b(br, 0x33334444, clock_now);
    chat::MeshConfig config;
    config.meshcore_send_profile = profile;
    useRoot(root / "a");
    a.applyProtocolConfig(protocol, config);
    a.setUserInfo("Alpha full name", "A");
    useRoot(root / "b");
    b.applyProtocolConfig(protocol, config);
    b.setUserInfo("Beta full name", "B");
    assert(a.isPkiReady() && b.isPkiReady());
    assert(a.getCapabilities().supports_pki && b.getCapabilities().supports_pki);
    useRoot(root / "a");
    assert(a.triggerDiscoveryAction(chat::MeshDiscoveryAction::ScanLocal));
    if (protocol == chat::MeshProtocol::MeshCore)
        assert(ar.sent.size() == 2); // Identity advert precedes anonymous discover request.
    for (const auto& packet : ar.sent) br.incoming.push_back(packet);
    useRoot(root / "b");
    br.fail_tx = protocol == chat::MeshProtocol::Meshtastic;
    b.tick();
    if (protocol == chat::MeshProtocol::Meshtastic)
    {
        assert(br.sent.size() == 1);
        br.fail_tx = false;
        useRoot(root / "a");
        assert(a.requestNodeInfo(0, true));
        useRoot(root / "b");
        br.incoming.push_back(ar.sent.back());
        b.tick();
        assert(br.sent.size() == 2); // A failed reply must not suppress the retry.
        useRoot(root / "a");
        assert(a.requestNodeInfo(0, true));
        useRoot(root / "b");
        br.incoming.push_back(ar.sent.back());
        clock_ms += 30000;
        b.tick(); // A suppressed request must not slide the successful-reply window.
        const auto before = br.sent.size();
        useRoot(root / "a");
        assert(a.requestNodeInfo(0, true));
        useRoot(root / "b");
        br.incoming.push_back(ar.sent.back());
        clock_ms += 30000;
        b.tick();
        assert(br.sent.size() == before + 1);
    }
    clock_ms += 2100;
    b.tick();
    assert(!br.sent.empty());
    // The scanner learns only DISCOVER_RESP, never B's signed advert.
    if (protocol == chat::MeshProtocol::Meshtastic)
    {
        // A due relay can be the latest RF output. Select B's addressed answer,
        // rather than feeding A its own relayed request as a discovery response.
        const auto answer = std::find_if(br.sent.rbegin(), br.sent.rend(), [&](const auto& packet)
                                         {
                                             chat::meshtastic::PacketHeaderWire header{};
                                             if (packet.size < sizeof(header)) return false;
                                             std::memcpy(&header, packet.data.data(), sizeof(header));
                                             return header.from == b.getNodeId() && header.to == a.getNodeId(); });
        assert(answer != br.sent.rend());
        ar.incoming.push_back(*answer);
    }
    else
        for (const auto& packet : br.sent) ar.incoming.push_back(packet);
    useRoot(root / "a");
    a.tick();
    if (!a.hasPkiKey(b.getNodeId()) || !b.hasPkiKey(a.getNodeId()))
        std::fprintf(stderr, "key exchange failed protocol=%u profile=%u a=%08x b=%08x a_has_b=%d b_has_a=%d replies=%zu\n",
                     static_cast<unsigned>(protocol), static_cast<unsigned>(profile),
                     a.getNodeId(), b.getNodeId(), a.hasPkiKey(b.getNodeId()), b.hasPkiKey(a.getNodeId()), br.sent.size());
    assert(a.hasPkiKey(b.getNodeId()) && b.hasPkiKey(a.getNodeId()));
    if (protocol == chat::MeshProtocol::MeshCore)
    {
        port::mesh::LinuxSqlitePeerKeyStore keys({"meshcore", "peer_keys", 64});
        mesh::PeerPublicKey persisted;
        assert(keys.get(mesh::NodeId{b.getNodeId()}, persisted).ok);
        assert(!persisted.verified); // Discovery is not manual verification.
    }
    Blob blob;
    chat::MeshPeerDirectoryCore directory(blob);
    chat::contacts::ContactService contacts(directory);
    contacts.begin();
    contacts.setActiveProtocol(protocol);
    drain(&contacts);
    std::vector<chat::contacts::PeerBrowseEntry> nearby;
    assert(contacts.browsePeers(chat::MeshPeerDirectoryView::Nearby, "", nearby).succeeded());
    assert(std::any_of(nearby.begin(), nearby.end(), [&](const auto& peer)
                       { return peer.node_id == b.getNodeId(); }));
    clock_ms += 3000; // Leave the five-second MC receive guard.
    a.tick();
    const auto sent = a.sendTextDetailed(chat::ChannelId::PRIMARY, "discovered private hello", 101, b.getNodeId());
    assert(sent.ok);
    br.incoming.push_back(ar.sent.back());
    useRoot(root / "b");
    b.tick();
    chat::MeshIncomingText incoming;
    assert(b.pollIncomingText(&incoming) && incoming.text == "discovered private hello");
    drain();
}
} // namespace

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-node-discovery-" + std::to_string(getpid()));
    sys::set_millis_provider(clock_now);
    sys::set_epoch_seconds_provider(epoch_now);
    sys::EventBus::init();
    samePrefixDiscovery();
    directoryPromotion();
    announcement(root / "announce-mt", chat::MeshProtocol::Meshtastic);
    announcement(root / "announce-mc", chat::MeshProtocol::MeshCore);
    exchange(root / "exchange-mt", chat::MeshProtocol::Meshtastic);
    exchange(root / "exchange-mc", chat::MeshProtocol::MeshCore);
    exchange(root / "exchange-mc-v1", chat::MeshProtocol::MeshCore, chat::MeshCorePayloadSendProfile::V1Only);
    sys::set_millis_provider(nullptr);
    sys::set_epoch_seconds_provider(nullptr);
    std::filesystem::remove_all(root);
}
