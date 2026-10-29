#include "app/linux_app_services.h"
#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/infra/reticulum/link_handshake_wire.h"
#include "chat/infra/reticulum/path_request_wire.h"
#include "chat/infra/rnode/rnode_packet_wire.h"
#include "chat/linux_mesh_worker.h"
#include "chat/linux_meshcore_group_wire.h"
#include "chat/linux_raw_lora_mesh_adapter.h"
#include "chat/linux_sqlite_chat_store.h"
#include "chat/ports/i_mesh_peer_directory.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "platform/linux/mesh/linux_sqlite_mesh_identity_store.h"
#include "platform/linux/packet_radio.h"
#include "platform/linux/runtime_paths.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "sys/event_bus.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <future>
#include <mutex>
#include <sqlite3.h>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;
namespace radio = platform::linux_runtime;
struct State
{
    std::mutex mutex;
    std::condition_variable wake;
    std::thread::id ui = std::this_thread::get_id();
    bool called_from_ui = false, online = false, block_acquire = false, acquire_entered = false;
    bool block_tx = false, tx_entered = false, fail_tx = false;
    bool block_rx = false;
    unsigned received = 0;
    std::deque<radio::RadioPacket> incoming;
    unsigned acquisitions = 0, releases = 0, transmissions = 0;
    std::vector<uint8_t> last_transmission;
    radio::RadioSettings settings;
    void io()
    {
        if (std::this_thread::get_id() == ui) called_from_ui = true;
    }
};
// Feed actual RNode air envelopes, rather than bypassing the carrier decoder.
void enqueueReticulum(State& state, const radio::RadioPacket& raw)
{
    chat::rnode::EncodedAirPacketSet air;
    assert(chat::rnode::encodeAirPacketSet(raw.data.data(), raw.size, 7, &air));
    radio::RadioPacket packet = raw;
    packet.size = air.first_len;
    std::memcpy(packet.data.data(), air.first, packet.size);
    state.incoming.push_back(packet);
    if (air.count == 2)
    {
        packet.size = air.second_len;
        std::memcpy(packet.data.data(), air.second, packet.size);
        state.incoming.push_back(packet);
    }
}
struct Driver final : radio::IPacketRadio
{
    State& state;
    explicit Driver(State& value) : state(value) {}
    const char* name() const override { return "worker test"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        state.io();
        ++state.acquisitions;
        state.acquire_entered = true;
        state.wake.notify_all();
        state.wake.wait(lock, [&]
                        { return !state.block_acquire; });
        state.online = true;
        return true;
    }
    void release() override
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.io();
        ++state.releases;
        state.online = false;
    }
    bool configureLoRa(const radio::RadioSettings& value) override
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.io();
        state.settings = value;
        return true;
    }
    bool startReceive() override
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.io();
        return true;
    }
    bool transmit(const uint8_t* bytes, std::size_t size) override
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        state.io();
        ++state.transmissions;
        state.last_transmission.assign(bytes, bytes + size);
        state.tx_entered = true;
        state.wake.notify_all();
        state.wake.wait(lock, [&]
                        { return !state.block_tx; });
        return !state.fail_tx;
    }
    bool pollReceive(radio::RadioPacket* packet) override
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        state.io();
        if (state.incoming.empty()) return false;
        *packet = state.incoming.front();
        state.incoming.pop_front();
        ++state.received;
        state.wake.notify_all();
        state.wake.wait(lock, [&]
                        { return !state.block_rx; });
        return true;
    }
    float readRssi() override
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.io();
        return -100;
    }
    bool isOnline() const override
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        return state.online;
    }
    const char* lastError() const override { return "injected transmission failure"; }
    radio::RadioSettings appliedLoRaConfig() const override
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        return state.settings;
    }
    radio::RadioStatistics stats() const override
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        radio::RadioStatistics result;
        result.online = state.online;
        result.lora_config = state.settings;
        return result;
    }
};
template <class Predicate>
void eventually(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    bool satisfied = predicate();
    while (!satisfied && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(1ms);
        satisfied = predicate();
    }
    assert(satisfied);
}
void unblock(State& state)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    state.block_acquire = state.block_tx = false;
    state.block_rx = false;
    state.wake.notify_all();
}
void testReticulumCarrier()
{
    using Adapter = trailmate::linux_app::LinuxRawLoraMeshAdapter;
    State state;
    Driver driver(state);
    Adapter adapter(driver, 123);
    chat::MeshConfig config;
    config.override_frequency_mhz = 433.775f;
    config.bandwidth_khz = 125;
    config.spread_factor = 9;
    config.coding_rate = 5;
    adapter.applyProtocolConfig(chat::MeshProtocol::Reticulum, config);
    assert(state.settings.freq_mhz == config.override_frequency_mhz && state.settings.sync_word == 0x12 &&
           state.settings.crc_len == 2 && state.settings.preamble_len == chat::rnode::recommendPreambleSymbols(125000, 9, 5));
    std::array<uint8_t, 501> payload;
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i);
    assert(adapter.sendReticulumControl(payload.data(), 254) && state.transmissions == 1 && state.last_transmission.size() == 255);
    assert(adapter.sendReticulumControl(payload.data(), 500) && state.transmissions == 3 && state.last_transmission.size() == 247);
    assert(!adapter.sendReticulumControl(payload.data(), 501) && state.transmissions == 3);
    state.fail_tx = true;
    assert(!adapter.sendReticulumControl(payload.data(), 500) && state.transmissions == 4);
    state.fail_tx = false;
    struct Capture
    {
        unsigned count = 0;
        Adapter::ReticulumPacket packet;
    } capture;
    adapter.setReticulumPacketSink([](void* context, const Adapter::ReticulumPacket& packet)
                                   {
        auto& capture = *static_cast<Capture*>(context);
        ++capture.count;
        capture.packet = packet; },
                                   &capture);
    chat::rnode::EncodedAirPacketSet air;
    assert(chat::rnode::encodeAirPacketSet(payload.data(), 500, 3, &air));
    radio::RadioPacket packet;
    packet.size = air.first_len;
    std::memcpy(packet.data.data(), air.first, packet.size);
    state.incoming.push_back(packet);
    adapter.tick();
    assert(capture.count == 0);
    packet.size = air.second_len;
    std::memcpy(packet.data.data(), air.second, packet.size);
    packet.rssi_dbm = -89;
    packet.snr_db = 4;
    state.incoming.push_back(packet);
    adapter.tick();
    assert(capture.count == 1 && capture.packet.size == 500 && capture.packet.rssi_dbm == -89 &&
           capture.packet.snr_db == 4 && !std::memcmp(capture.packet.data.data(), payload.data(), 500));
    // An incomplete frame cannot survive a radio/protocol settings change.
    packet.size = air.first_len;
    std::memcpy(packet.data.data(), air.first, packet.size);
    state.incoming.push_back(packet);
    adapter.tick();
    config.override_frequency_mhz += 1;
    adapter.applyProtocolConfig(chat::MeshProtocol::Reticulum, config);
    packet.size = air.second_len;
    std::memcpy(packet.data.data(), air.second, packet.size);
    state.incoming.push_back(packet);
    adapter.tick();
    assert(capture.count == 1);
    adapter.setReticulumPacketSink(nullptr, nullptr);
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-mesh-worker-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_GPS_DEVICE", "/dev/trailmate-test-missing", 1);
    testReticulumCarrier();
    mesh::PeerPublicKey known;
    known.node_id = mesh::NodeId{777};
    std::memset(known.public_key, 0x11, sizeof(known.public_key));
    radio::mesh::LinuxSqlitePeerKeyStore peer_store;
    assert(peer_store.put(known).ok);
    State state;
    radio::PacketRadioCoordinator coordinator(std::make_unique<Driver>(state));
    trailmate::linux_app::LinuxMeshWorker worker(coordinator.mesh(), 123);
    assert(worker.initialize());
    assert(state.acquisitions == 0); // Bootstrap creates identity, not a device lease.
    state.block_acquire = true;
    chat::MeshConfig config;
    worker.applyProtocolConfig(chat::MeshProtocol::Meshtastic, config);
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        assert(state.wake.wait_for(lock, 2s, [&]
                                   { return state.acquire_entered; }));
    }
    auto getter = std::async(std::launch::async, [&]
                             {
        assert(!worker.isReady());
        (void)worker.statusText(); (void)worker.getCapabilities(); (void)worker.getNodeId();
        (void)coordinator.enabled(); (void)coordinator.mesh().hardwareCandidatePresent();
        (void)coordinator.mesh().isOnline(); (void)coordinator.mesh().stats();
        (void)coordinator.mesh().lastError(); (void)coordinator.mesh().appliedLoRaConfig(); });
    const bool responsive = getter.wait_for(1s) == std::future_status::ready;
    if (!responsive) unblock(state);
    getter.get();
    assert(responsive);
    worker.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
    const auto meshcore_node = worker.getNodeId();
    assert(meshcore_node && meshcore_node != 123);
    unblock(state);
    eventually([&]
               { return worker.isReady(); });
    trailmate::linux_app::meshcore_wire::Frame received_frame;
    assert(trailmate::linux_app::meshcore_wire::encodeText(config, chat::ChannelId::PRIMARY,
                                                           "keep across rename", 1700000000, received_frame));
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        radio::RadioPacket packet;
        packet.size = received_frame.size;
        std::memcpy(packet.data.data(), received_frame.bytes.data(), packet.size);
        state.block_rx = true;
        state.incoming.push_back(packet);
    }
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        assert(state.wake.wait_for(lock, 2s, [&]
                                   { return state.received != 0; }));
    }
    worker.setUserInfo("Renamed", "RN");
    assert(worker.requestNodeInfo(0xFFFFFFFFU, false));
    unblock(state);
    chat::MeshIncomingText kept;
    eventually([&]
               { return worker.pollIncomingText(&kept); });
    assert(kept.text == "keep across rename");
    eventually([&]
               { std::lock_guard<std::mutex> lock(state.mutex); return state.transmissions != 0; });
    eventually([&]
               { return worker.isReady(); });
    unsigned before_requests;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        before_requests = state.transmissions;
        assert(state.settings.sf == config.meshcore_sf && !state.called_from_ui);
        state.block_tx = true;
        state.tx_entered = false;
    }
    // A full unrelated event bus must not lose reliable send outcomes.
    assert(sys::EventBus::init());
    for (unsigned i = 0; i < 32; ++i) assert(sys::EventBus::publish(new sys::Event(sys::EventType::SystemTick), 0));
    assert(worker.sendTextDetailed(chat::ChannelId::PRIMARY, "in flight", 101).ok);
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        assert(state.wake.wait_for(lock, 2s, [&]
                                   { return state.tx_entered; }));
    }
    for (unsigned i = 0; i < 32; ++i) assert(worker.sendTextDetailed(chat::ChannelId::PRIMARY, "queued", 200 + i).ok);
    assert(worker.sendTextDetailed(chat::ChannelId::PRIMARY, "full", 300).failure == chat::MeshOperationFailure::Busy);
    getter = std::async(std::launch::async, [&]
                        {
        (void)worker.statusText(); (void)worker.radioStatsText(); (void)worker.diagnosticLines();
        (void)coordinator.mesh().isOnline(); (void)coordinator.mesh().stats();
        (void)coordinator.mesh().lastError(); (void)coordinator.mesh().appliedLoRaConfig();
        assert(coordinator.enabled()); });
    const bool tx_responsive = getter.wait_for(1s) == std::future_status::ready;
    if (!tx_responsive) unblock(state);
    getter.get();
    assert(tx_responsive);
    auto retuned = config;
    ++retuned.meshcore_sf;
    worker.applyProtocolConfig(chat::MeshProtocol::MeshCore, retuned);
    assert(!worker.isReady());
    trailmate::linux_app::LinuxMeshWorker::Outcome outcome;
    unsigned cancelled = 0;
    while (worker.takeOutcome(outcome))
    {
        assert(outcome.message >= 200 && outcome.message < 232 && outcome.status == chat::MessageStatus::Failed);
        assert(outcome.protocol == chat::MeshProtocol::MeshCore);
        ++cancelled;
    }
    assert(cancelled == 32);
    unblock(state);
    eventually([&]
               { return worker.isReady(); });
    eventually([&]
               { return worker.takeOutcome(outcome); });
    assert(outcome.message == 101 && outcome.status == chat::MessageStatus::Sent);
    assert(outcome.protocol == chat::MeshProtocol::MeshCore);
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        assert(state.transmissions == before_requests + 1 && state.settings.sf == retuned.meshcore_sf);
        state.fail_tx = true;
    }
    assert(worker.sendTextDetailed(chat::ChannelId::PRIMARY, "will fail", 301).ok);
    eventually([&]
               { return worker.takeOutcome(outcome); });
    assert(outcome.message == 301 && outcome.status == chat::MessageStatus::Failed);
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.fail_tx = false;
        state.block_tx = true;
        state.tx_entered = false;
    }
    assert(worker.sendTextDetailed(chat::ChannelId::PRIMARY, "old protocol", 401).ok);
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        assert(state.wake.wait_for(lock, 2s, [&]
                                   { return state.tx_entered; }));
    }
    worker.applyProtocolConfig(chat::MeshProtocol::Meshtastic, config);
    assert(worker.getNodeId() == 123 && !worker.isReady());
    unblock(state);
    eventually([&]
               { return worker.isReady(); });
    eventually([&]
               { return worker.takeOutcome(outcome); });
    assert(outcome.message == 401 && outcome.protocol == chat::MeshProtocol::MeshCore);
    assert(outcome.status == chat::MessageStatus::Sent);
    sys::Event* event;
    while (sys::EventBus::subscribe(&event)) delete event;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.fail_tx = true;
    }
    assert(worker.startKeyVerification(777));
    eventually([&]
               { return sys::EventBus::subscribe(&event); });
    assert(event->type == sys::EventType::KeyVerificationFinal);
    const auto* failure = static_cast<sys::KeyVerificationFinalEvent*>(event);
    assert(failure->runtime_failure && failure->has_protocol && failure->protocol == chat::MeshProtocol::Meshtastic);
    assert(failure->verification_code[0] == '\0');
    delete event;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.fail_tx = false;
    }
    sqlite3* identity_blocker = nullptr;
    assert(sqlite3_open(radio::sqlite_database_path().c_str(), &identity_blocker) == SQLITE_OK);
    assert(sqlite3_exec(identity_blocker, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK);
    worker.applyProtocolConfig(chat::MeshProtocol::Reticulum, config);
    std::this_thread::sleep_for(30ms);
    chat::ReticulumLocalIdentityInfo pending_identity;
    const auto identity_read_started = std::chrono::steady_clock::now();
    assert(!worker.getReticulumLocalIdentityInfo(&pending_identity));
    assert(std::chrono::steady_clock::now() - identity_read_started < 100ms);
    assert(sqlite3_exec(identity_blocker, "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(identity_blocker);
    eventually([&]
               { return worker.isReady() && worker.getCapabilities().supports_reticulum_destination_text; });
    assert(worker.isReady());
    assert(worker.sendTextDetailed(chat::ChannelId::PRIMARY, "unsupported").failure == chat::MeshOperationFailure::Unsupported);
    chat::ReticulumLocalIdentityInfo reticulum;
    eventually([&]
               { return worker.getReticulumLocalIdentityInfo(&reticulum); });
    assert(reticulum.ready && reticulum.node_id == worker.getNodeId());
    assert(std::any_of(std::begin(reticulum.propagation_address), std::end(reticulum.propagation_address), [](uint8_t byte)
                       { return byte != 0; }));
    const auto retained_reticulum = reticulum;
    {
        radio::LinuxReticulumIdentity link_peer(root / "link-peer.sqlite");
        assert(link_peer.begin());
        uint8_t payload[67];
        std::memcpy(payload, link_peer.publicKey().data(), 64);
        chat::reticulum::buildLinkSignalling(500, payload + 64);
        radio::RadioPacket request;
        request.size = request.data.size();
        assert(chat::reticulum::buildHeader1Packet(chat::reticulum::PacketType::LinkRequest,
                                                   chat::reticulum::DestinationType::Single, chat::reticulum::PacketContext::None, false,
                                                   reticulum.lxmf_address, payload, sizeof(payload), request.data.data(), &request.size));
        unsigned before;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            before = state.transmissions;
            enqueueReticulum(state, request);
        }
        eventually([&]
                   { std::lock_guard<std::mutex> lock(state.mutex); return state.transmissions > before; });
        uint8_t link_id[16], shared[32], link_key[64];
        assert(chat::reticulum::computeLinkRequestId(request.data.data(), request.size, link_id));
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            chat::rnode::ParsedAirPacket air;
            chat::reticulum::ParsedPacket proof;
            assert(chat::rnode::parseAirPacket(state.last_transmission.data(), state.last_transmission.size(), &air));
            assert(chat::reticulum::parsePacket(air.payload, air.payload_len, &proof));
            assert(proof.packet_type == chat::reticulum::PacketType::Proof && proof.context == 0xff);
            assert(link_peer.sharedSecret(proof.payload + 64, shared));
            assert(chat::reticulum::hkdfSha256(shared, sizeof(shared), link_id, 16, nullptr, 0, link_key, sizeof(link_key)));
        }
        const auto encrypted = [&](chat::reticulum::PacketContext context, const uint8_t* bytes, size_t size)
        {
            uint8_t iv[16]{}; // Deterministic fake peer only.
            std::array<uint8_t, 500> token{};
            size_t token_size = token.size();
            assert(chat::reticulum::tokenEncrypt(link_key, iv, bytes, size, token.data(), &token_size));
            radio::RadioPacket packet;
            packet.size = packet.data.size();
            assert(chat::reticulum::buildHeader1Packet(chat::reticulum::PacketType::Data, chat::reticulum::DestinationType::Link,
                                                       context, false, link_id, token.data(), token_size, packet.data.data(), &packet.size));
            return packet;
        };
        const uint8_t rtt[] = {0xcb, 0x3f, 0xf0, 0, 0, 0, 0, 0, 0};
        uint8_t identify[128], identity_domain[80];
        std::memcpy(identify, link_peer.publicKey().data(), 64);
        std::memcpy(identity_domain, link_id, 16);
        std::memcpy(identity_domain + 16, identify, 64);
        assert(link_peer.sign(identity_domain, sizeof(identity_domain), identify + 64));
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            enqueueReticulum(state, encrypted(chat::reticulum::PacketContext::LrRtt, rtt, sizeof(rtt)));
            enqueueReticulum(state, encrypted(chat::reticulum::PacketContext::LinkIdentify, identify, sizeof(identify)));
        }
        uint8_t body[200], signed_body[500], message_hash[32], signature[64], envelope[500];
        size_t body_size = sizeof(body), signed_size = sizeof(signed_body), envelope_size = sizeof(envelope);
        assert(chat::lxmf::encodeTextPayload(1700000000, "", "worker durable LXMF", body, &body_size));
        assert(chat::lxmf::buildSignedPart(reticulum.lxmf_address, link_peer.destinationHash().data(), body, body_size,
                                           signed_body, &signed_size, message_hash));
        assert(link_peer.sign(signed_body, signed_size, signature));
        assert(chat::lxmf::packMessage(reticulum.lxmf_address, link_peer.destinationHash().data(), signature,
                                       body, body_size, envelope, &envelope_size));
        const auto text_packet = encrypted(chat::reticulum::PacketContext::None, envelope, envelope_size);
        trailmate::linux_app::LinuxSqliteChatStore store;
        chat::ChatModel model;
        chat::ChatService service(model, worker, store, chat::MeshProtocol::Reticulum);
        (void)store.loadConversationPage(0, 0, nullptr); // Initialize the actual chat schema.
        sqlite3* database = nullptr;
        assert(sqlite3_open(radio::sqlite_database_path().c_str(), &database) == SQLITE_OK);
        assert(sqlite3_exec(database, "CREATE TRIGGER reject_lxmf BEFORE INSERT ON chat_messages BEGIN SELECT RAISE(ABORT,'injected LXMF storage failure'); END;",
                            nullptr, nullptr, nullptr) == SQLITE_OK);
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            before = state.transmissions;
            enqueueReticulum(state, text_packet);
        }
        eventually([&]
                   {
            service.processIncoming();
            const auto lines = worker.diagnosticLines();
            return std::any_of(lines.begin(), lines.end(), [](const auto& line) { return line.find("awaiting_commit=1") != std::string::npos; }); });
        assert(!store.hasReticulumLxmfMessageHash(message_hash));
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            assert(state.transmissions == before); // No transport acceptance before durable application commit.
        }
        assert(sqlite3_exec(database, "DROP TRIGGER reject_lxmf;", nullptr, nullptr, nullptr) == SQLITE_OK);
        assert(sqlite3_close(database) == SQLITE_OK);
        eventually([&]
                   {
            service.processIncoming();
            std::lock_guard<std::mutex> lock(state.mutex);
            return state.transmissions > before; });
        assert(store.hasReticulumLxmfMessageHash(message_hash));
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            chat::rnode::ParsedAirPacket air;
            chat::reticulum::ParsedPacket proof;
            uint8_t expected_packet_hash[32];
            chat::reticulum::computePacketHash(text_packet.data.data(), text_packet.size, expected_packet_hash);
            assert(chat::rnode::parseAirPacket(state.last_transmission.data(), state.last_transmission.size(), &air));
            assert(chat::reticulum::parsePacket(air.payload, air.payload_len, &proof));
            assert(proof.packet_type == chat::reticulum::PacketType::Proof && proof.context == 0 && proof.payload_len == 96 &&
                   !std::memcmp(proof.payload, expected_packet_hash, 32));
            before = state.transmissions;
            enqueueReticulum(state, text_packet);
        }
        chat::ConversationId conversation(chat::ChannelId::PRIMARY, 0, chat::MeshProtocol::Reticulum);
        conversation.reticulum_identity = chat::makeReticulumPeerIdentity(link_peer.destinationHash().data(), link_peer.identityHash().data());
        const int unread = store.getUnread(conversation);
        eventually([&]
                   {
            service.processIncoming();
            std::lock_guard<std::mutex> lock(state.mutex);
            return state.transmissions > before; });
        assert(store.getUnread(conversation) == unread && store.loadRecent(conversation, 10).size() == 1);
        assert(worker.isReady());
    }
    const char* encoded_announce =
#include "reticulum_announce_first.inc"
        ;
    radio::RadioPacket announced;
    for (size_t i = 0; encoded_announce[i]; i += 2)
    {
        char byte[3] = {encoded_announce[i], encoded_announce[i + 1], 0};
        announced.data[announced.size++] = static_cast<uint8_t>(std::strtoul(byte, nullptr, 16));
    }
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        enqueueReticulum(state, announced);
    }
    eventually([&]
               {
        const auto diagnostics = worker.diagnosticLines();
        return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& line)
        { return line.find("Reticulum RX paths=1 accepted=1") != std::string::npos; }); });
    assert(worker.isReady());
    chat::reticulum::ParsedPacket remote_announce;
    assert(chat::reticulum::parsePacket(announced.data.data(), announced.size, &remote_announce));
    chat::IMeshAdapter& adapter_port = worker;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.block_tx = true;
        state.tx_entered = false;
    }
    assert(adapter_port.requestDirectoryPath(remote_announce.destination_hash));
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        assert(state.wake.wait_for(lock, 2s, [&]
                                   { return state.tx_entered; }));
        chat::reticulum::ParsedPacket request;
        chat::rnode::ParsedAirPacket air;
        assert(chat::rnode::parseAirPacket(state.last_transmission.data(), state.last_transmission.size(), &air) && !air.split);
        assert(chat::reticulum::parsePacket(air.payload, air.payload_len, &request));
        assert(request.packet_type == chat::reticulum::PacketType::Data && request.destination_type == chat::reticulum::DestinationType::Plain &&
               request.payload_len == 32 && !std::memcmp(request.payload, remote_announce.destination_hash, 16));
    }
    const auto control_read = std::chrono::steady_clock::now();
    assert(worker.getReticulumLocalIdentityInfo(&reticulum) && worker.isReady());
    assert(std::chrono::steady_clock::now() - control_read < 100ms);
    unblock(state);
    const auto path_counter = [&](const char* expected)
    {
        const auto lines = worker.diagnosticLines();
        return std::any_of(lines.begin(), lines.end(), [&](const auto& line)
                           { return line.find(expected) != std::string::npos; });
    };
    eventually([&]
               { return path_counter("path requests sent=1"); });
    auto response = announced;
    response.data[18] = static_cast<uint8_t>(chat::reticulum::PacketContext::PathResponse);
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        enqueueReticulum(state, response);
    }
    eventually([&]
               { return path_counter("responses=1"); });
    assert(worker.isReady());
    std::array<uint8_t, 16> retry_target;
    std::memcpy(retry_target.data(), remote_announce.destination_hash, retry_target.size());
    retry_target[0] ^= 1;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.fail_tx = true;
    }
    assert(adapter_port.requestDirectoryPath(retry_target.data()));
    eventually([&]
               { return path_counter("failed=1"); });
    std::vector<uint8_t> failed_control;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        failed_control = state.last_transmission;
        state.fail_tx = false;
    }
    assert(adapter_port.requestDirectoryPath(retry_target.data()));
    eventually([&]
               { return path_counter("path requests sent=2"); });
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        chat::rnode::ParsedAirPacket retry_air, failed_air;
        assert(chat::rnode::parseAirPacket(state.last_transmission.data(), state.last_transmission.size(), &retry_air));
        assert(chat::rnode::parseAirPacket(failed_control.data(), failed_control.size(), &failed_air));
        chat::reticulum::ParsedPacket retry_packet, failed_packet;
        assert(chat::reticulum::parsePacket(retry_air.payload, retry_air.payload_len, &retry_packet));
        assert(chat::reticulum::parsePacket(failed_air.payload, failed_air.payload_len, &failed_packet));
        assert(retry_packet.payload_len == 32 && failed_packet.payload_len == 32);
        assert(std::memcmp(retry_packet.payload + 16, failed_packet.payload + 16, 16));
    }
    config.tx_enabled = false;
    worker.applyProtocolConfig(chat::MeshProtocol::Reticulum, config);
    eventually([&]
               { return worker.getReticulumLocalIdentityInfo(&reticulum); });
    assert(!adapter_port.requestDirectoryPath(retry_target.data()));
    config.tx_enabled = true;
    config.reticulum_lora_enabled = false;
    worker.applyProtocolConfig(chat::MeshProtocol::Reticulum, config);
    eventually([&]
               { return worker.getReticulumLocalIdentityInfo(&reticulum); });
    assert(!adapter_port.requestDirectoryPath(retry_target.data()));
    unsigned policy_transmissions;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        assert(!state.online);
        policy_transmissions = state.transmissions;
    }
    config.reticulum_lora_enabled = true;
    config.reticulum_interface_policy = chat::ReticulumInterfacePolicy::WifiGatewayOnly;
    worker.applyProtocolConfig(chat::MeshProtocol::Reticulum, config);
    eventually([&]
               { return worker.getReticulumLocalIdentityInfo(&reticulum); });
    assert(!adapter_port.requestDirectoryPath(retry_target.data()));
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        assert(!state.online && state.transmissions == policy_transmissions);
    }
    config.reticulum_interface_policy = chat::ReticulumInterfacePolicy::All;
    worker.applyProtocolConfig(chat::MeshProtocol::Meshtastic, config);
    assert(!adapter_port.requestDirectoryPath(remote_announce.destination_hash));
    assert(!worker.getReticulumLocalIdentityInfo(&reticulum) && !reticulum.ready);
    eventually([&]
               { return worker.isReady(); });
    worker.shutdown();
    assert(!worker.isReady());
    assert(!worker.getReticulumLocalIdentityInfo(&reticulum));
    assert(worker.initialize());
    eventually([&]
               { return worker.isReady(); });
    worker.applyProtocolConfig(chat::MeshProtocol::Reticulum, config);
    eventually([&]
               { return worker.getReticulumLocalIdentityInfo(&reticulum); });
    assert(!std::memcmp(reticulum.identity_hash, retained_reticulum.identity_hash, sizeof(reticulum.identity_hash)) &&
           !std::memcmp(reticulum.lxmf_address, retained_reticulum.lxmf_address, sizeof(reticulum.lxmf_address)) && worker.isReady());
    worker.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
    assert(worker.getNodeId() == meshcore_node);
    eventually([&]
               { return worker.isReady(); });
    worker.shutdown();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        assert(!state.called_from_ui && state.acquisitions == state.releases);
    }
    while (sys::EventBus::subscribe(&event)) delete event;
    {
        trailmate::linux_app::LinuxAppServicesOptions options;
        options.runtime_mode = radio::LinuxRuntimeMode::DeviceRealMesh;
        options.packet_radio = &coordinator.mesh();
        trailmate::linux_app::LinuxAppServices services(options);
        assert(services.initialize());
        assert(services.switchMeshProtocol(chat::MeshProtocol::MeshCore, false));
        eventually([&]
                   { return services.meshAdapter()->isReady(); });
        unsigned before_broadcast;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            before_broadcast = state.transmissions;
        }
        services.broadcastNodeInfo();
        eventually([&]
                   { std::lock_guard<std::mutex> lock(state.mutex); return state.transmissions > before_broadcast; });
        const auto sent = services.getChatService().sendTextDetailed(chat::ChannelId::PRIMARY, "service worker send");
        assert(sent.ok);
        eventually([&]
                   {
            services.dispatchPendingEvents();
            const auto* message = services.getChatService().getMessageForProtocol(sent.msg_id, chat::MeshProtocol::MeshCore);
            return message && message->status == chat::MessageStatus::Sent; });
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.fail_tx = true;
        }
        const auto rejected = services.getChatService().sendTextDetailed(chat::ChannelId::PRIMARY, "physical failure");
        assert(rejected.ok); // Accepted by the queue, never falsely recorded as physically sent.
        bool ever_sent = false;
        eventually([&]
                   {
            services.dispatchPendingEvents();
            const auto* message = services.getChatService().getMessageForProtocol(rejected.msg_id, chat::MeshProtocol::MeshCore);
            if (message && message->status == chat::MessageStatus::Sent) ever_sent = true;
            return message && message->status == chat::MessageStatus::Failed; });
        assert(!ever_sent);
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.fail_tx = false;
        }
        const auto closing = services.getChatService().sendTextDetailed(chat::ChannelId::PRIMARY, "close after queued send");
        assert(closing.ok);
        assert(services.switchMeshProtocol(chat::MeshProtocol::Reticulum, false));
        eventually([&]
                   { return services.meshAdapter()->getReticulumLocalIdentityInfo(&reticulum); });
        const char* person_encoded =
#include "reticulum_announce_contact.inc"
            ;
        radio::RadioPacket person_announcement;
        for (size_t i = 0; person_encoded[i]; i += 2)
        {
            char byte[3] = {person_encoded[i], person_encoded[i + 1], 0};
            assert(person_announcement.size < person_announcement.data.size());
            person_announcement.data[person_announcement.size++] = static_cast<uint8_t>(std::strtoul(byte, nullptr, 16));
        }
        chat::reticulum::ParsedPacket person_packet;
        chat::reticulum::ParsedAnnounce peer_announce;
        assert(chat::reticulum::parsePacket(person_announcement.data.data(), person_announcement.size, &person_packet));
        assert(chat::reticulum::parseAnnounce(person_packet, &peer_announce));
        uint8_t peer_identity_hash[16];
        chat::reticulum::computeIdentityHash(peer_announce.public_key, peer_identity_hash);
        const auto public_identity = chat::makeReticulumPeerIdentity(person_packet.destination_hash, peer_identity_hash);
        auto& directory = platform::ui::reticulum_directory::mesh_peer_directory();
        const auto removed = directory.remove(chat::makeMeshPeerReticulumIdentity(public_identity));
        assert(removed.succeeded() || removed.code == chat::MeshPeerDirectoryStatusCode::NotFound);
        services.getContactService().clearCache();
        assert(services.getContactService().getReticulumContactName(public_identity).empty());
        const auto peer_projection_started = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            enqueueReticulum(state, person_announcement);
        }
        eventually([&]
                   {
            services.dispatchPendingEvents();
            return !services.getContactService().getReticulumContactName(public_identity).empty(); });
        assert(std::chrono::steady_clock::now() - peer_projection_started < 500ms);
        chat::MeshPeerRecord observed;
        assert(platform::ui::reticulum_directory::mesh_peer_directory().find(chat::makeMeshPeerReticulumIdentity(public_identity), observed).succeeded());
        assert(observed.reticulum.has_public_keys && !observed.flags.trusted && !observed.flags.favorite);
        assert(services.meshAdapter()->isReady());
        services.shutdown();
        const auto* terminal = services.getChatService().getMessageForProtocol(closing.msg_id, chat::MeshProtocol::MeshCore);
        assert(terminal && terminal->status != chat::MessageStatus::Queued);
    }
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        assert(!state.called_from_ui && state.acquisitions == state.releases);
    }
    assert(platform::ui::reticulum_directory::mesh_peer_directory().flush().succeeded());
    std::filesystem::remove_all(root);
}
