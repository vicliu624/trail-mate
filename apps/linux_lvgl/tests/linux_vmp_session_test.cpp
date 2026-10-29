#include "chat/infra/voice/vmp_control_auth.h"
#include "chat/infra/voice/vmp_media_frames.h"
#include "chat/infra/voice/vmp_receive_block.h"
#include "platform/linux/vmp_radio.h"
#include "platform/linux/vmp_session.h"
#include "platform/linux/voice_store.h"
#include <cassert>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <unistd.h>
#include <vector>

using namespace platform::linux_runtime;
namespace vmp = chat::voice::vmp;
using Result = NativeVmpSession::Result;
struct Clock final : IVmpClock
{
    uint64_t time = 0, cancel_at = 0;
    std::atomic_bool* cancel = nullptr;
    uint64_t nowMs() const override { return time; }
    void sleepMs(unsigned ms) override
    {
        time += ms;
        if (cancel && cancel_at && time >= cancel_at) *cancel = true;
    }
};
struct State
{
    bool online = false, media = false;
    unsigned acquires = 0;
    RadioSettings settings;
    std::deque<RadioPacket> control_rx, media_rx;
    std::vector<RadioPacket> sent;
    std::function<void(const RadioPacket&, bool)> on_tx;
    std::function<void()> on_media;
};
class Radio final : public IPacketRadio
{
  public:
    explicit Radio(std::shared_ptr<State> state) : state(std::move(state)) {}
    const char* name() const override { return "VMP peer fixture"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override
    {
        ++state->acquires;
        state->online = true;
        return true;
    }
    void release() override { state->online = false; }
    bool configureLoRa(const RadioSettings& value) override
    {
        state->media = false;
        state->settings = value;
        return true;
    }
    bool supportsFsk() const override { return true; }
    bool supportsFskConfiguration(const FskSettings& value) const override { return valid_lr2021_fsk_settings(value); }
    bool configureFsk(const FskSettings&) override
    {
        state->media = true;
        if (state->on_media) state->on_media();
        return true;
    }
    bool startReceive() override { return true; }
    bool transmit(const uint8_t* bytes, size_t size) override
    {
        RadioPacket packet;
        assert(size <= packet.data.size());
        packet.size = size;
        std::copy_n(bytes, size, packet.data.data());
        state->sent.push_back(packet);
        if (state->on_tx) state->on_tx(packet, state->media);
        return true;
    }
    bool pollReceive(RadioPacket* out) override
    {
        auto& queue = state->media ? state->media_rx : state->control_rx;
        if (queue.empty()) return false;
        *out = queue.front();
        queue.pop_front();
        return true;
    }
    float readRssi() override { return -100; }
    bool isOnline() const override { return state->online; }
    const char* lastError() const override { return "fixture error"; }
    RadioSettings appliedLoRaConfig() const override { return state->settings; }
    RadioStatistics stats() const override { return {}; }
    std::shared_ptr<State> state;
};
struct Fixture
{
    std::shared_ptr<State> state = std::make_shared<State>();
    PacketRadioCoordinator radio{std::make_unique<Radio>(state)};
    Clock clock;
    vmp::FixedVerifiedContactSecretDirectory contacts;
    std::array<uint8_t, 32> secret{};
    Fixture()
    {
        secret.fill(0x53); // Public test provisioning fixture, never a device key.
        assert(contacts.upsertVerifiedContactSecret(100, secret.data()));
        assert(contacts.upsertVerifiedContactSecret(200, secret.data()));
        assert(radio.mesh().acquire());
        RadioSettings baseline;
        baseline.freq_mhz = 433.175f;
        assert(radio.mesh().configureLoRa(baseline));
    }
    ~Fixture() { radio.mesh().release(); }
    void restored() { assert(!state->media && radio.mesh().isOnline() && state->settings.freq_mhz == 433.175f); }
};
VoiceClip media()
{
    VoiceClip out;
    out.size = out.bytes.size();
    for (size_t i = 0; i < out.size; ++i) out.bytes[i] = static_cast<uint8_t>(i * 13 + 1);
    return out;
}
RadioPacket control(const vmp::ControlFrame& value, const vmp::PrivateSessionKeys* keys = nullptr,
                    vmp::PrivateFrameDirection direction = vmp::PrivateFrameDirection::SenderToReceiver)
{
    RadioPacket packet;
    packet.size = packet.data.size();
    assert(keys ? vmp::encodePrivateControlFrame(value, *keys, direction, packet.data.data(), &packet.size)
                : vmp::encodePublicControlFrame(value, packet.data.data(), &packet.size));
    return packet;
}
RadioPacket ready(const vmp::ControlFrame& value, vmp::DataType type, const vmp::PrivateSessionKeys* keys = nullptr,
                  vmp::PrivateFrameDirection direction = vmp::PrivateFrameDirection::SenderToReceiver)
{
    vmp::DataHeader header;
    header.type = type;
    header.session_id = value.session_id;
    RadioPacket packet;
    packet.size = packet.data.size();
    if (keys)
    {
        packet.size = vmp::kDataHeaderSize;
        assert(vmp::encodeDataHeader(header, packet.data.data(), &packet.size));
        assert(vmp::tagPrivateReady(*keys, value.session_nonce, direction, header, packet.data.data() + packet.size));
        packet.size += vmp::kPrivateDataAuthTagSize;
    }
    else assert(vmp::buildPublicReadyFrame(header, packet.data.data(), &packet.size));
    return packet;
}
struct Commit final : IVmpIncomingCommit
{
    unsigned calls = 0;
    bool accepts = true;
    VoiceClip clip;
    vmp::ControlFrame received;
    LinuxVoiceStore* store = nullptr;
    uint64_t local_id = 0;
    bool accepts_reservation = true;
    std::function<void()> on_reserved;
    Reservation reservePrivateControl(const std::array<uint8_t, 32>& identifier) override
    {
        if (!store || !accepts_reservation) return Reservation::Unavailable;
        const auto result = store->reservePrivateControl(identifier);
        if (result == LinuxVoiceStore::Result::Ready && on_reserved) on_reserved();
        return result == LinuxVoiceStore::Result::Ready ? Reservation::Reserved : result == LinuxVoiceStore::Result::Duplicate ? Reservation::Replayed
                                                                                                                               : Reservation::Unavailable;
    }
    bool commit(const vmp::ControlFrame& value, const VoiceClip& bytes) override
    {
        ++calls;
        received = value;
        clip = bytes;
        if (!accepts) return false;
        return !store || store->append(value, bytes, false, 42, vmp::VoicePresentationProtocol::MeshCore,
                                       value.conversation_channel, local_id) == LinuxVoiceStore::Result::Ready;
    }
};
void sender(bool private_mode, bool bad_accept = false, bool silent_peer = false)
{
    Fixture peer;
    const auto clip = media();
    NativeVmpSession session(peer.radio.foreground(), &peer.contacts, &peer.clock);
    assert(session.prepare({100, private_mode ? 200u : 0u, 3, 0}, clip) == Result::Prepared);
    const auto offered = session.control();
    vmp::PrivateSessionKeys keys{};
    vmp::EphemeralKeyPair ephemeral{};
    vmp::ReceiveBlock block;
    vmp::MediaLayout layout;
    assert(vmp::planMediaLayout(clip.size, &layout) && block.begin(layout));
    unsigned probes = 0, shards = 0;
    VoiceClip recovered;
    peer.state->on_tx = [&](const RadioPacket& packet, bool in_media)
    {
        if (!in_media)
        {
            vmp::ControlFrame decoded;
            if (private_mode)
            {
                assert(vmp::derivePrivateControlKey(peer.secret.data(), offered.session_nonce, offered.session_id, keys.control_key));
                assert(vmp::decodePrivateControlFrame(packet.data.data(), packet.size, keys, vmp::PrivateFrameDirection::SenderToReceiver, &decoded));
                assert(vmp::generateEphemeralKeyPair(&ephemeral));
                assert(vmp::derivePrivateSessionKeys(peer.secret.data(), ephemeral.private_key, decoded.ephemeral_public_key,
                                                     decoded.session_nonce, decoded.session_id, &keys));
                auto accepted = decoded;
                accepted.type = vmp::ControlType::Accept;
                accepted.sender_id = 200;
                accepted.target_id = 100;
                std::copy_n(ephemeral.public_key, 32, accepted.ephemeral_public_key);
                if (bad_accept) accepted.conversation_channel = 4;
                if (!silent_peer) peer.state->control_rx.push_back(control(accepted, &keys, vmp::PrivateFrameDirection::ReceiverToSender));
            }
            else assert(vmp::decodePublicControlFrame(packet.data.data(), packet.size, &decoded));
            assert(decoded.session_id == offered.session_id && decoded.conversation_channel == 3);
            return;
        }
        assert(peer.clock.time >= offered.data_start_delay_ms);
        vmp::DataHeader header;
        assert(vmp::decodeDataHeader(packet.data.data(), vmp::kDataHeaderSize, &header));
        if (header.type == vmp::DataType::ReadyProbe)
        {
            ++probes;
            if (private_mode)
            {
                assert(vmp::verifyPrivateReadyTag(keys, offered.session_nonce, vmp::PrivateFrameDirection::SenderToReceiver,
                                                  header, packet.data.data() + vmp::kDataHeaderSize));
                peer.state->media_rx.push_back(ready(offered, vmp::DataType::Ready, &keys, vmp::PrivateFrameDirection::ReceiverToSender));
            }
            else assert(vmp::parsePublicReadyFrame(packet.data.data(), packet.size, &header));
            return;
        }
        assert(header.type == vmp::DataType::Shard && header.shard_index == shards++);
        const uint8_t* shard = nullptr;
        std::array<uint8_t, 160> plain;
        if (private_mode)
        {
            assert(vmp::openPrivateShard(keys, offered.session_nonce, vmp::PrivateFrameDirection::SenderToReceiver, header,
                                         packet.data.data() + vmp::kDataHeaderSize, plain.size(),
                                         packet.data.data() + vmp::kDataHeaderSize + plain.size(), plain.data()));
            shard = plain.data();
        }
        else assert(vmp::parsePublicShardFrame(packet.data.data(), packet.size, &header, &shard));
        if (!block.recovered() && block.accept(header, shard, 160) == vmp::ReceiveBlockResult::Complete)
            assert(block.recover(recovered.bytes.data(), recovered.bytes.size(), &recovered.size));
    };
    std::atomic_bool cancel{false};
    const auto result = session.send(cancel);
    if (bad_accept || silent_peer) assert(result == Result::Timeout && !shards && !probes);
    else
    {
        assert(result == Result::Complete && shards == 10 && probes == (private_mode ? 1u : 3u));
        assert(recovered.size == clip.size && recovered.bytes == clip.bytes);
    }
    assert(session.send(cancel) == Result::Invalid); // No replay/resend API.
    peer.restored();
    vmp::clearPrivateSessionKeys(&keys);
}
void receiver(bool private_mode, unsigned losses = 2, bool bad_tag = false, bool storage_failure = false)
{
    Fixture peer;
    const auto clip = media();
    vmp::ControlFrame offered;
    offered.type = private_mode ? vmp::ControlType::Offer : vmp::ControlType::Announce;
    offered.flags = private_mode ? vmp::ControlFlagPrivate : vmp::ControlFlagBroadcast | vmp::ControlFlagPublicBroadcast;
    offered.sender_id = 100;
    offered.target_id = private_mode ? 200 : vmp::kBroadcastTargetId;
    offered.session_id = 0x123456789abcdef0ULL;
    offered.session_nonce[0] = 1;
    offered.encoded_media_len = clip.size;
    offered.total_blocks = 1;
    offered.phy_profile_id = 1;
    offered.data_start_delay_ms = private_mode ? 120 : 700;
    vmp::EphemeralKeyPair ephemeral{};
    vmp::PrivateSessionKeys keys{};
    if (private_mode)
    {
        assert(vmp::generateEphemeralKeyPair(&ephemeral));
        std::copy_n(ephemeral.public_key, 32, offered.ephemeral_public_key);
        assert(vmp::derivePrivateControlKey(peer.secret.data(), offered.session_nonce, offered.session_id, keys.control_key));
    }
    const auto envelope = control(offered, private_mode ? &keys : nullptr);
    vmp::TransmitBlock block;
    assert(block.prepare(clip.bytes.data(), clip.size));
    unsigned ready_replies = 0;
    peer.state->on_tx = [&](const RadioPacket& packet, bool in_media)
    {
        if (!in_media)
        {
            vmp::ControlFrame accepted;
            assert(private_mode && vmp::decodePrivateControlFrame(packet.data.data(), packet.size, keys,
                                                                  vmp::PrivateFrameDirection::ReceiverToSender, &accepted));
            assert(accepted.type == vmp::ControlType::Accept && accepted.sender_id == 200 && accepted.target_id == 100);
            assert(vmp::derivePrivateSessionKeys(peer.secret.data(), ephemeral.private_key, accepted.ephemeral_public_key,
                                                 offered.session_nonce, offered.session_id, &keys));
        }
        else
        {
            vmp::DataHeader header;
            assert(private_mode && vmp::decodeDataHeader(packet.data.data(), vmp::kDataHeaderSize, &header));
            assert(header.type == vmp::DataType::Ready && vmp::verifyPrivateReadyTag(keys, offered.session_nonce,
                                                                                     vmp::PrivateFrameDirection::ReceiverToSender, header, packet.data.data() + vmp::kDataHeaderSize));
            ++ready_replies;
        }
    };
    peer.state->on_media = [&]
    {
        if (private_mode)
        {
            peer.state->media_rx.push_back(ready(offered, vmp::DataType::ReadyProbe, &keys));
            peer.state->media_rx.push_back(ready(offered, vmp::DataType::ReadyProbe, &keys));
        }
        // Broadcast data remains recoverable even when every READY_PROBE is lost.
        for (uint8_t i = 0; i < 10; ++i)
        {
            if (i < losses) continue;
            RadioPacket packet;
            packet.size = packet.data.size();
            assert(private_mode ? block.buildPrivateShardFrame(keys, offered.session_nonce, offered.session_id, i, packet.data.data(), &packet.size)
                                : block.buildPublicShardFrame(offered.session_id, i, packet.data.data(), &packet.size));
            if (bad_tag && i == losses) packet.data[packet.size - 1] ^= 1;
            peer.state->media_rx.push_back(packet);
        }
    };
    NativeVmpSession session(peer.radio.foreground(), &peer.contacts, &peer.clock);
    Commit commit;
    char scratch[] = "/tmp/trailmate-vmp-commit-XXXXXX";
    assert(mkdtemp(scratch));
    const auto database = std::filesystem::path(scratch) / "voice.sqlite3";
    LinuxVoiceStore store(database);
    assert(store.open() == LinuxVoiceStore::Result::Ready);
    commit.store = &store;
    commit.accepts = !storage_failure;
    std::atomic_bool cancel{false};
    const auto result = session.receive(envelope.data.data(), envelope.size, 200, commit, cancel);
    if (losses > 2 || bad_tag) assert(result == Result::Timeout && !commit.calls);
    else
    {
        assert(result == (storage_failure ? Result::StorageFailure : Result::Complete) && commit.calls == 1);
        assert(commit.clip.size == clip.size && commit.clip.bytes == clip.bytes);
    }
    assert(private_mode ? ready_replies == 2 : peer.state->sent.empty());
    std::array<vmp::VoiceMessageMetadata, 8> rows;
    const auto retained = store.list(rows.data(), rows.size());
    if (result == Result::Complete)
    {
        assert(retained == 1 && commit.local_id == rows[0].local_id);
        assert(vmp::voiceMessageSourceUnverified(rows[0]) == !private_mode && !vmp::voiceMessageOutgoing(rows[0]));
        LinuxVoiceStore reopened(database);
        assert(reopened.open() == LinuxVoiceStore::Result::Ready);
        VoiceClip restored;
        assert(reopened.media(commit.local_id, restored) && restored.size == clip.size && restored.bytes == clip.bytes);
    }
    else assert(!retained && !commit.local_id);
    const auto transmissions = peer.state->sent.size();
    assert(session.send(cancel) == Result::Invalid && peer.state->sent.size() == transmissions);
    if (private_mode)
    {
        LinuxVoiceStore reopened(database);
        assert(reopened.open() == LinuxVoiceStore::Result::Ready);
        Commit replay;
        replay.store = &reopened;
        NativeVmpSession restarted(peer.radio.foreground(), &peer.contacts, &peer.clock);
        assert(restarted.receive(envelope.data.data(), envelope.size, 200, replay, cancel) == Result::Ignored);
        assert(!replay.calls && peer.state->sent.size() == transmissions);
    }
    peer.restored();
    vmp::clearPrivateSessionKeys(&keys);
    std::filesystem::remove_all(scratch);
}
int main()
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    sender(false);
    sender(true);
    sender(true, true);
    sender(true, false, true);
    receiver(false);
    receiver(true);
    receiver(false, 3);
    receiver(true, 3);
    receiver(false, 2, true);
    receiver(true, 2, true);
    receiver(true, 2, false, true);
    {
        Fixture peer;
        NativeVmpSession sender_plan(peer.radio.foreground(), &peer.contacts, &peer.clock);
        assert(sender_plan.prepare({100, 200, 0, 0}, media()) == Result::Prepared);
        const auto offered = sender_plan.control();
        vmp::PrivateSessionKeys keys{};
        assert(vmp::derivePrivateControlKey(peer.secret.data(), offered.session_nonce, offered.session_id, keys.control_key));
        auto envelope = control(offered, &keys);
        sender_plan.discard();
        Commit commit;
        std::atomic_bool cancel{false};
        NativeVmpSession unverified(peer.radio.foreground(), nullptr, &peer.clock);
        assert(unverified.receive(envelope.data.data(), envelope.size, 200, commit, cancel) == Result::Unverified);
        NativeVmpSession receiver(peer.radio.foreground(), &peer.contacts, &peer.clock);
        assert(receiver.receive(envelope.data.data(), envelope.size, 300, commit, cancel) == Result::Ignored);
        assert(receiver.receive(envelope.data.data(), envelope.size, 200, commit, cancel) == Result::StorageFailure);
        envelope.data[envelope.size - 1] ^= 1;
        assert(receiver.receive(envelope.data.data(), envelope.size, 200, commit, cancel) == Result::CryptoFailure);
        assert(!commit.calls && peer.state->sent.empty() && peer.state->acquires == 1);
        envelope.data[envelope.size - 1] ^= 1;
        char directory[] = "/tmp/trailmate-vmp-reserve-cancel-XXXXXX";
        assert(mkdtemp(directory));
        LinuxVoiceStore store(std::filesystem::path(directory) / "voice.sqlite3");
        assert(store.open() == LinuxVoiceStore::Result::Ready);
        commit.store = &store;
        commit.on_reserved = [&]
        { cancel = true; };
        assert(receiver.receive(envelope.data.data(), envelope.size, 200, commit, cancel) == Result::Cancelled);
        cancel = false;
        commit.on_reserved = {};
        assert(receiver.receive(envelope.data.data(), envelope.size, 200, commit, cancel) == Result::Ignored);
        auto delayed = offered;
        ++delayed.session_id;
        assert(vmp::derivePrivateControlKey(peer.secret.data(), delayed.session_nonce, delayed.session_id, keys.control_key));
        const auto late = control(delayed, &keys);
        commit.on_reserved = [&]
        { peer.clock.time += 1600; };
        assert(receiver.receive(late.data.data(), late.size, 200, commit, cancel) == Result::Timeout);
        assert(!commit.calls && peer.state->sent.empty());
        std::filesystem::remove_all(directory);
        peer.restored();
        vmp::clearPrivateSessionKeys(&keys);
    }
    {
        Fixture peer;
        NativeVmpSession session(peer.radio.foreground(), nullptr, &peer.clock);
        assert(session.prepare({100, 200, 0, 0}, media()) == Result::Unverified && peer.state->sent.empty());
        assert(session.prepare({100, 0, 0, 0}, media()) == Result::Prepared);
        std::atomic_bool cancel{true};
        assert(session.send(cancel) == Result::Cancelled && peer.state->sent.empty());
        assert(session.prepare({100, 0, 0, 0}, media()) == Result::Prepared);
        cancel = false;
        peer.clock.cancel = &cancel;
        peer.clock.cancel_at = 100;
        assert(session.send(cancel) == Result::Cancelled && peer.state->sent.size() == 1);
        peer.restored();
    }
#else
    Fixture peer;
    NativeVmpSession session(peer.radio.foreground(), nullptr, &peer.clock);
    assert(session.prepare({100, 0, 0, 0}, media()) == Result::CryptoFailure && peer.state->sent.empty());
    receiver(false);
#endif
}
