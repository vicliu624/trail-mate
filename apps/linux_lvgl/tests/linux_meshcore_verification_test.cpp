#include "chat/infra/mesh_peer_directory_core.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/linux_meshcore_direct_wire.h"
#include "chat/linux_meshcore_verification.h"
#include "chat/linux_raw_lora_mesh_adapter.h"
#include "chat/usecase/contact_service.h"
#include "platform/linux/mesh/linux_sqlite_mesh_identity_store.h"
#include "sys/event_bus.h"
#include "ui_key_verification_runtime/key_verification_action_sink.h"
#include "ui_key_verification_runtime/key_verification_presentation_source.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <memory>
#include <unistd.h>

namespace wire = trailmate::linux_app::meshcore_wire;
using Verification = wire::Verification;
using Prompt = Verification::Prompt;
using namespace platform::linux_runtime;
std::array<uint8_t, 12> control(uint8_t type, uint64_t nonce)
{
    std::array<uint8_t, 12> out{'T', 'M', 2, type};
    for (unsigned i = 0; i < 8; ++i) out[4 + i] = uint8_t(nonce >> (8 * i));
    return out;
}
uint64_t nonce(const uint8_t* bytes)
{
    uint64_t result = 0;
    for (unsigned i = 0; i < 8; ++i) result |= uint64_t(bytes[4 + i]) << (8 * i);
    return result;
}
void stateTests()
{
    for (bool tagged : {false, true})
        for (uint8_t type : {1, 2, 3})
        {
            // Fixed control payload inside either production appdata wrapper.
            // This nonce ends in six zero bytes; padding must not eat them.
            std::array<uint8_t, 32> padded{};
            const size_t prefix = tagged ? 7 : 4;
            if (tagged)
            {
                padded[0] = 0xda;
                padded[1] = 0x7a;
                padded[2] = 1;
                padded[3] = 12;
            }
            else padded[0] = 12;
            const auto payload = control(type, 0x222);
            std::copy(payload.begin(), payload.end(), padded.begin() + prefix);
            assert(chat::meshcore::trimPeerPlaintextPadding(7, padded.data(), padded.size()) == prefix + 12);
            padded.back() = 1;
            assert(chat::meshcore::trimPeerPlaintextPadding(7, padded.data(), padded.size()) == 0);
            padded.back() = 0;
            padded[tagged ? 3 : 0] = 13;
            assert(chat::meshcore::trimPeerPlaintextPadding(7, padded.data(), padded.size()) < prefix + 12);
        }
    std::array<uint8_t, 32> shared{}, key{};
    for (unsigned i = 0; i < 32; ++i) shared[i] = uint8_t(i);
    key.fill(0x53);
    uint32_t number = 0;
    // Independent Python hashlib fixture: SHA256(secret || LE32(lo) ||
    // LE32(hi) || LE64(nonce)), LE32(digest[:4]) % 1000000.
    assert(Verification::number(shared.data(), 100, 200, 0x1122334455667788ULL, number) && number == 784229);
    uint32_t reverse = 0;
    assert(Verification::number(shared.data(), 200, 100, 0x1122334455667788ULL, reverse) && reverse == number);
    assert(!Verification::number(shared.data(), 100, 100, 1, reverse));
    Verification sender, receiver;
    Verification::Action init, ready, request, final, compare, ignored;
    assert(sender.start(200, key, 123, number, 0xffffff00u, init) && init.send && init.payload == control(1, 123));
    assert(!sender.start(300, key, 456, number, 0xffffff01u, ignored));
    assert(receiver.receive(100, key, init.payload.data(), 12, number, 0xffffff00u, ready));
    assert(ready.prompt == Prompt::ShowNumber && ready.send && !ready.sender && ready.payload == control(2, 123));
    assert(!receiver.receive(100, key, init.payload.data(), 12, number, 0xffffff01u, ignored));
    assert(!sender.receive(201, key, ready.payload.data(), 12, 0, 1, ignored));
    auto wrong = control(2, 124);
    assert(!sender.receive(200, key, wrong.data(), 12, 0, 1, ignored));
    assert(sender.receive(200, key, ready.payload.data(), 12, 0, 1, request) && request.prompt == Prompt::EnterNumber && !request.send);
    assert(!sender.receive(200, key, ready.payload.data(), 12, 0, 2, ignored));
    assert(!sender.submit(200, key, 123, number + 1000000, 3, ignored));
    assert(!sender.submit(200, key, 123, number - 1, 3, ignored));
    assert(sender.submit(200, key, 123, number, 4, final) && final.prompt == Prompt::Compare && final.sender && final.send);
    assert(receiver.receive(100, key, final.payload.data(), 12, 0, 4, compare) && compare.prompt == Prompt::Compare && !compare.sender);
    assert(!receiver.receive(100, key, final.payload.data(), 12, 0, 5, ignored));
    assert(!receiver.receive(100, key, init.payload.data(), 12, number, 6, ignored));
    assert(sender.start(200, key, 456, number, 10, init));
    assert(!sender.expire(60009, ignored));
    assert(sender.expire(60010, ignored) && ignored.prompt == Prompt::Failed && !ignored.send);
    assert(!sender.expire(70000, ignored));
    auto malformed = control(1, 0);
    assert(!receiver.receive(100, key, malformed.data(), 12, number, 8, ignored));
    malformed = control(1, 789);
    malformed[2] = 9;
    assert(!receiver.receive(100, key, malformed.data(), 12, number, 8, ignored));
    assert(!receiver.receive(100, key, init.payload.data(), 11, number, 8, ignored));
    assert(receiver.receive(100, key, init.payload.data(), 12, number, 9, ready));
    auto changed = key;
    changed[0] ^= 1;
    assert(!receiver.matches(changed));
    assert(receiver.cancel(ignored) && ignored.prompt == Prompt::Failed);
    assert(!receiver.receive(100, key, init.payload.data(), 12, number, 10, ignored));
}
struct Radio final : IPacketRadio
{
    bool online = false, fail = false;
    RadioSettings settings;
    std::deque<RadioPacket> incoming;
    std::vector<wire::Frame> sent;
    const char* name() const override { return "MC verification fixture"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override
    {
        online = true;
        return true;
    }
    void release() override { online = false; }
    bool configureLoRa(const RadioSettings& value) override
    {
        settings = value;
        return true;
    }
    bool startReceive() override { return true; }
    bool transmit(const uint8_t* bytes, size_t size) override
    {
        wire::Frame frame;
        assert(size <= frame.bytes.size());
        frame.size = size;
        std::copy_n(bytes, size, frame.bytes.data());
        sent.push_back(frame);
        return !fail;
    }
    bool pollReceive(RadioPacket* packet) override
    {
        if (incoming.empty()) return false;
        *packet = incoming.front();
        incoming.pop_front();
        return true;
    }
    float readRssi() override { return -100; }
    bool isOnline() const override { return online; }
    const char* lastError() const override { return "fixture failure"; }
    RadioSettings appliedLoRaConfig() const override { return settings; }
    RadioStatistics stats() const override { return {}; }
};
uint32_t time_ms = 100;
uint32_t clockMs() { return time_ms; }
void drain()
{
    sys::Event* event = nullptr;
    while (sys::EventBus::subscribe(&event)) delete event;
}
template <class Event>
std::unique_ptr<Event> take(sys::EventType type)
{
    sys::Event* event = nullptr;
    while (sys::EventBus::subscribe(&event))
    {
        if (event->type == type)
        {
            std::unique_ptr<Event> result(static_cast<Event*>(event));
            assert(result->has_protocol && result->protocol == chat::MeshProtocol::MeshCore);
            return result;
        }
        delete event;
    }
    assert(false && "Missing verification event");
    return {};
}
struct Memory final : chat::IMeshPeerDirectoryBlobStore
{
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(std::vector<uint8_t>&) override { return chat::MeshPeerDirectoryBlobLoadResult::Missing; }
    bool saveBlob(const uint8_t*, size_t) override { return true; }
    void clearBlob() override {}
};
void adapterTests(chat::MeshCorePayloadSendProfile profile)
{
    char path[] = "/tmp/trailmate-mc-verification-XXXXXX";
    assert(mkdtemp(path));
    const auto other = std::filesystem::path(path) / "peer";
    setenv("TRAIL_MATE_SETTINGS_ROOT", other.c_str(), 1);
    wire::Identity peer;
    assert(peer.begin());
    setenv("TRAIL_MATE_SETTINGS_ROOT", path, 1);
    setenv("TRAIL_MATE_CACHE_ROOT", path, 1);
    wire::Identity local;
    assert(local.begin());
    platform::linux_runtime::mesh::LinuxSqlitePeerKeyStore store({"meshcore", "peer_keys", 64});
    ::mesh::PeerPublicKey key;
    key.node_id = ::mesh::NodeId{peer.nodeId()};
    std::copy(peer.publicKey().begin(), peer.publicKey().end(), key.public_key);
    assert(store.put(key).ok);
    Radio radio;
    trailmate::linux_app::LinuxRawLoraMeshAdapter adapter(radio, 777, clockMs);
    chat::MeshConfig config;
    config.meshcore_send_profile = profile;
    adapter.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
    assert(sys::EventBus::init());
    assert(adapter.startKeyVerification(peer.nodeId()));
    wire::DirectIncoming offered;
    assert(wire::decodeDirect(peer, local.publicKey(), radio.sent.back().bytes.data(), radio.sent.back().size, offered));
    assert(offered.port == 12 && offered.size == 12 && offered.payload[3] == 1 && offered.want_ack);
    const auto session = nonce(offered.payload.data());
    uint8_t shared[32]{};
    uint32_t expected = 0;
    assert(peer.sharedSecret(local.publicKey().data(), shared) && Verification::number(shared, peer.nodeId(), local.nodeId(), session, expected));
    const auto deliver = [&](const std::array<uint8_t, 12>& payload, bool corrupt = false)
    {
        wire::Frame frame;
        uint32_t signature = 0;
        assert(wire::encodeDirectData(peer, local.publicKey(), offered.profile, 12, payload.data(), payload.size(), true, frame, signature));
        RadioPacket packet;
        packet.size = frame.size;
        std::copy_n(frame.bytes.data(), frame.size, packet.data.data());
        if (corrupt) packet.data[packet.size - 1] ^= 1;
        radio.incoming.push_back(packet);
        adapter.tick();
    };
    deliver(control(2, session), true);
    assert(sys::EventBus::pendingCount() == 0);
    deliver(control(2, session));
    auto request = take<sys::KeyVerificationNumberRequestEvent>(sys::EventType::KeyVerificationNumberRequest);
    assert(request->node_id == peer.nodeId() && request->nonce == session);
    const auto before = radio.sent.size();
    assert(!adapter.submitKeyVerificationNumber(peer.nodeId(), session, expected + 1000000));
    assert(radio.sent.size() == before);
    assert(adapter.submitKeyVerificationNumber(peer.nodeId(), session, expected));
    auto final = take<sys::KeyVerificationFinalEvent>(sys::EventType::KeyVerificationFinal);
    assert(!final->runtime_failure && final->is_sender && final->nonce == session);
    wire::DirectIncoming finished;
    assert(wire::decodeDirect(peer, local.publicKey(), radio.sent.back().bytes.data(), radio.sent.back().size, finished));
    assert(finished.port == 12 && finished.size == 12 && finished.payload[3] == 3 && nonce(finished.payload.data()) == session);
    chat::MeshIncomingData incoming;
    assert(!adapter.pollIncomingData(&incoming));

    // Exercise the real presentation source and user acceptance port. Merely
    // completing the carrier never marks a new contact manually verified.
    Memory memory;
    chat::MeshPeerDirectoryCore directory(memory);
    chat::contacts::ContactService contacts(directory);
    contacts.begin();
    contacts.setActiveProtocol(chat::MeshProtocol::MeshCore);
    chat::MeshPeerRecord contact;
    contact.valid = true;
    contact.identity = chat::makeMeshPeerNodeIdentity(chat::MeshProtocol::MeshCore, peer.nodeId());
    contact.meshcore.has_public_key = true;
    contact.meshcore.node_id_hint = peer.nodeId();
    std::copy(peer.publicKey().begin(), peer.publicKey().end(), contact.meshcore.public_key);
    assert(contacts.recordPeer(contact));
    ui_key_verification_runtime::KeyVerificationSessionAdapter ui_session;
    ui_key_verification_runtime::KeyVerificationPresentationSource source(ui_session, &contacts, &adapter);
    ui_key_verification_runtime::KeyVerificationActionSink actions(ui_session, &adapter, &contacts);
    assert(!actions.accept(peer.nodeId()).ok);
    source.onFinal(final->node_id, final->nonce, final->is_sender, final->verification_code);
    assert(!contacts.getPeerByNodeId(peer.nodeId())->key_manually_verified);
    assert(actions.accept(peer.nodeId()).ok && contacts.getPeerByNodeId(peer.nodeId())->key_manually_verified);
    assert(contacts.setNodeKeyManuallyVerified(peer.nodeId(), false));

    // Receiver side: authenticated INIT -> number/READY -> compare after FINAL.
    drain();
    deliver(control(1, 0x222));
    auto inform = take<sys::KeyVerificationNumberInformEvent>(sys::EventType::KeyVerificationNumberInform);
    assert(inform->node_id == peer.nodeId() && inform->nonce == 0x222);
    deliver(control(3, 0x223));
    assert(sys::EventBus::pendingCount() == 0);
    deliver(control(3, 0x222));
    final = take<sys::KeyVerificationFinalEvent>(sys::EventType::KeyVerificationFinal);
    assert(!final->is_sender && !final->runtime_failure && final->nonce == 0x222);
    source.onFinal(final->node_id, final->nonce, final->is_sender, final->verification_code);
    assert(actions.reject(peer.nodeId()).ok && !contacts.getPeerByNodeId(peer.nodeId())->key_manually_verified);
    assert(!adapter.pollIncomingData(&incoming));

    drain();
    assert(adapter.startKeyVerification(peer.nodeId()));
    time_ms += 60000;
    adapter.tick();
    auto failure = take<sys::KeyVerificationFinalEvent>(sys::EventType::KeyVerificationFinal);
    assert(failure->runtime_failure);
    assert(adapter.startKeyVerification(peer.nodeId()));
    adapter.applyProtocolConfig(chat::MeshProtocol::Meshtastic, config);
    failure = take<sys::KeyVerificationFinalEvent>(sys::EventType::KeyVerificationFinal);
    assert(failure->runtime_failure);
    adapter.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
    radio.fail = true;
    assert(!adapter.startKeyVerification(peer.nodeId()));
    failure = take<sys::KeyVerificationFinalEvent>(sys::EventType::KeyVerificationFinal);
    assert(failure->runtime_failure);
    drain();
    std::filesystem::remove_all(path);
}
int main()
{
    stateTests();
    adapterTests(chat::MeshCorePayloadSendProfile::V1Only);
    adapterTests(chat::MeshCorePayloadSendProfile::V2Only);
}
