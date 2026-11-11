#include "chat/infra/meshtastic/mt_codec_pb.h"
#include "chat/infra/meshtastic/mt_pki_crypto.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/linux_raw_lora_mesh_adapter.h"
#include "platform/linux/mesh/linux_sqlite_mesh_identity_store.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <openssl/evp.h>
#include <pb_encode.h>
#include <unistd.h>

namespace mt = chat::meshtastic;
namespace runtime = platform::linux_runtime;
using Adapter = trailmate::linux_app::LinuxRawLoraMeshAdapter;
using Failure = chat::delivery::SendFailureKind;
constexpr uint32_t self = 0x12345678, peer = 0xA1B3B57C;
uint32_t now_ms = 100;
uint32_t clock_ms() { return now_ms; }

struct Radio final : runtime::IPacketRadio
{
    bool online = false, tx_ok = true;
    size_t transmissions = 0;
    runtime::RadioPacket last_sent;
    runtime::RadioSettings settings{};
    std::deque<runtime::RadioPacket> incoming;
    const char* name() const override { return "ACK test radio"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override
    {
        online = true;
        return true;
    }
    void release() override { online = false; }
    bool configureLoRa(const runtime::RadioSettings& value) override
    {
        settings = value;
        return true;
    }
    bool startReceive() override { return online; }
    bool transmit(const uint8_t* bytes, size_t size) override
    {
        assert(size <= last_sent.data.size());
        last_sent.size = size;
        std::memcpy(last_sent.data.data(), bytes, size);
        ++transmissions;
        return tx_ok;
    }
    bool pollReceive(runtime::RadioPacket* out) override
    {
        if (incoming.empty()) return false;
        *out = incoming.front();
        incoming.pop_front();
        return true;
    }
    float readRssi() override { return -90; }
    bool isOnline() const override { return online; }
    const char* lastError() const override { return tx_ok ? "" : "test TX failed"; }
    runtime::RadioSettings appliedLoRaConfig() const override { return settings; }
    runtime::RadioStatistics stats() const override { return {}; }
};
struct Result
{
    uint32_t id;
    chat::MessageStatus status;
    Failure failure;
};
void result_sink(void* context, uint32_t id, chat::MeshProtocol protocol, chat::MessageStatus status, Failure failure)
{
    assert(protocol == chat::MeshProtocol::Meshtastic);
    static_cast<std::vector<Result>*>(context)->push_back({id, status, failure});
}
mesh::LocalIdentity identity(uint8_t seed)
{
    mesh::LocalIdentity value{};
    for (size_t i = 0; i < 32; ++i) value.private_key[i] = uint8_t(seed + i);
    EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, value.private_key, 32);
    assert(key);
    size_t length = 32;
    assert(EVP_PKEY_get_raw_public_key(key, value.public_key, &length) == 1 && length == 32);
    EVP_PKEY_free(key);
    value.valid = true;
    return value;
}
std::array<uint8_t, 32> shared_key(const mesh::LocalIdentity& remote, const mesh::LocalIdentity& local)
{
    EVP_PKEY* private_key = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, remote.private_key, 32);
    EVP_PKEY* public_key = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, local.public_key, 32);
    assert(private_key && public_key);
    EVP_PKEY_CTX* context = EVP_PKEY_CTX_new(private_key, nullptr);
    std::array<uint8_t, 32> shared{};
    size_t size = shared.size();
    assert(context && EVP_PKEY_derive_init(context) == 1 && EVP_PKEY_derive_set_peer(context, public_key) == 1 &&
           EVP_PKEY_derive(context, shared.data(), &size) == 1 && size == 32);
    EVP_PKEY_CTX_free(context);
    EVP_PKEY_free(private_key);
    EVP_PKEY_free(public_key);
    mt::hashSharedKey(shared.data(), shared.size());
    return shared;
}
runtime::RadioPacket routing_packet(const std::array<uint8_t, 32>& shared, uint32_t request,
                                    meshtastic_Routing_Error reason = meshtastic_Routing_Error_NONE,
                                    bool pki = true, uint32_t from = peer, uint32_t to = self,
                                    int variant = 1)
{
    static uint32_t sequence = 0x10000;
    const auto id = ++sequence;
    meshtastic_Data data = meshtastic_Data_init_default;
    data.portnum = meshtastic_PortNum_ROUTING_APP;
    data.request_id = request;
    meshtastic_Routing routing = meshtastic_Routing_init_default;
    routing.which_variant = variant ? meshtastic_Routing_error_reason_tag : 0;
    routing.error_reason = reason;
    auto routing_stream = pb_ostream_from_buffer(data.payload.bytes, sizeof(data.payload.bytes));
    assert(pb_encode(&routing_stream, meshtastic_Routing_fields, &routing));
    data.payload.size = routing_stream.bytes_written;
    if (variant == 2)
    {
        data.payload.bytes[0] = 0x80;
        data.payload.size = 1;
    }
    uint8_t plaintext[256]{};
    auto stream = pb_ostream_from_buffer(plaintext, sizeof(plaintext));
    assert(pb_encode(&stream, meshtastic_Data_fields, &data));
    uint8_t payload[256]{}, psk[32]{};
    size_t payload_size = stream.bytes_written, psk_size = 0;
    if (pki)
    {
        constexpr uint32_t extra = 0x71345;
        uint8_t nonce[mt::kPkiNonceSize]{};
        mt::initPkiNonce(from, id, extra, nonce);
        assert(mt::encryptPkiAesCcm(shared.data(), shared.size(), nonce, 8, nullptr, 0,
                                    plaintext, payload_size, payload, payload + payload_size));
        payload_size += 8;
        for (unsigned i = 0; i < 4; ++i) payload[payload_size++] = uint8_t(extra >> (8 * i));
    }
    else
    {
        std::memcpy(payload, plaintext, payload_size);
        mt::expandShortPsk(1, psk, &psk_size);
    }
    runtime::RadioPacket packet{};
    packet.size = packet.data.size();
    assert(mt::buildWirePacket(payload, payload_size, from, id, to, pki ? 0 : 0x08, 2, false,
                               nullptr, 0, packet.data.data(), &packet.size));
    if (!pki)
    {
        uint8_t nonce[16]{};
        const uint64_t packet_id = id;
        std::memcpy(nonce, &packet_id, sizeof(packet_id));
        std::memcpy(nonce + 8, &from, sizeof(from));
        EVP_CIPHER_CTX* cipher = EVP_CIPHER_CTX_new();
        int count = 0, tail = 0;
        auto* bytes = packet.data.data() + sizeof(mt::PacketHeaderWire);
        assert(cipher && psk_size == 16 && EVP_EncryptInit_ex(cipher, EVP_aes_128_ctr(), nullptr, psk, nonce) == 1 &&
               EVP_EncryptUpdate(cipher, bytes, &count, payload, payload_size) == 1 &&
               EVP_EncryptFinal_ex(cipher, bytes + count, &tail) == 1 && size_t(count + tail) == payload_size);
        EVP_CIPHER_CTX_free(cipher);
    }
    return packet;
}
int main()
{
    // Current official firmware consumes want_response from bitfield bit 1.
    // Exercise serialized bytes, including reuse of caller-owned scratch.
    for (bool want_response : {true, false})
    {
        uint8_t encoded[256]{};
        size_t size = sizeof(encoded);
        meshtastic_Data scratch = meshtastic_Data_init_default;
        assert(mt::encodeNodeInfoMessage("!12345678", "Node", "ND", meshtastic_HardwareModel_PRIVATE_HW,
                                         nullptr, nullptr, 0, want_response, encoded, &size, &scratch));
        meshtastic_Data decoded = meshtastic_Data_init_default;
        auto stream = pb_istream_from_buffer(encoded, size);
        assert(pb_decode(&stream, meshtastic_Data_fields, &decoded));
        assert(decoded.want_response == want_response && decoded.has_bitfield);
        assert(((decoded.bitfield & 0x02) != 0) == want_response);
        assert((decoded.bitfield & 0x01) == 0); // Never grant MQTT forwarding implicitly.
        const uint8_t payload[] = {1, 2, 3};
        size = sizeof(encoded);
        assert(mt::encodeAppDataWithRequestId(meshtastic_PortNum_KEY_VERIFICATION_APP, payload, sizeof(payload),
                                              want_response, 0x1234, encoded, &size, &scratch));
        decoded = meshtastic_Data_init_default;
        stream = pb_istream_from_buffer(encoded, size);
        assert(pb_decode(&stream, meshtastic_Data_fields, &decoded));
        assert(decoded.request_id == 0x1234 && decoded.want_response == want_response && decoded.has_bitfield);
        assert(((decoded.bitfield & 0x02) != 0) == want_response && (decoded.bitfield & 0x01) == 0);
    }
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-mt-ack-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    const auto local = identity(0x37), remote = identity(0x51);
    runtime::mesh::LinuxSqliteLocalIdentityStore local_store;
    assert(local_store.save(local).ok);
    runtime::mesh::LinuxSqlitePeerKeyStore peer_store;
    mesh::PeerPublicKey peer_key{};
    peer_key.node_id = mesh::NodeId{peer};
    std::copy(std::begin(remote.public_key), std::end(remote.public_key), peer_key.public_key);
    assert(peer_store.put(peer_key).ok);
    peer_key.node_id = mesh::NodeId{peer + 1}; // Same test key, different authenticated peer ID.
    assert(peer_store.put(peer_key).ok);
    const auto shared = shared_key(remote, local);
    Radio radio;
    Adapter adapter(radio, self, clock_ms);
    chat::MeshConfig config{};
    adapter.applyProtocolConfig(chat::MeshProtocol::Meshtastic, config);
    assert(adapter.begin() && adapter.isPkiReady() && adapter.hasPkiKey(peer));
    // Independently decrypt the resident adapter's real channel packets with
    // OpenSSL, then compare the entire frame with the ESP/shared wire codec.
    // This prevents two matching mistakes in a codec round-trip from passing.
    for (const uint8_t key_size : {uint8_t{16}, uint8_t{32}})
    {
        auto channel_config = config;
        channel_config.primary_key_len = key_size;
        for (unsigned i = 0; i < key_size; ++i) channel_config.primary_key[i] = uint8_t(i + 1);
        adapter.applyProtocolConfig(chat::MeshProtocol::Meshtastic, channel_config);
        const uint8_t payload[] = {0x31, 0, 0x42, 0x80, 0xff};
        assert(adapter.sendAppData(chat::ChannelId::PRIMARY, 303, payload, sizeof(payload)));
        mt::PacketHeaderWire header;
        std::memcpy(&header, radio.last_sent.data.data(), sizeof(header));
        assert(header.from == self && header.to == UINT32_MAX);
        uint8_t nonce[16]{};
        const uint64_t packet_id = header.id;
        std::memcpy(nonce, &packet_id, sizeof(packet_id));
        std::memcpy(nonce + 8, &header.from, sizeof(header.from));
        std::array<uint8_t, 256> plain{};
        auto* cipher = EVP_CIPHER_CTX_new();
        int count = 0, tail = 0;
        const size_t encrypted_size = radio.last_sent.size - sizeof(header);
        assert(cipher && EVP_DecryptInit_ex(cipher, key_size == 16 ? EVP_aes_128_ctr() : EVP_aes_256_ctr(),
                                            nullptr, channel_config.primary_key, nonce) == 1);
        assert(EVP_DecryptUpdate(cipher, plain.data(), &count, radio.last_sent.data.data() + sizeof(header),
                                 int(encrypted_size)) == 1 &&
               EVP_DecryptFinal_ex(cipher, plain.data() + count, &tail) == 1 && size_t(count + tail) == encrypted_size);
        EVP_CIPHER_CTX_free(cipher);
        meshtastic_Data decoded = meshtastic_Data_init_default;
        auto stream = pb_istream_from_buffer(plain.data(), encrypted_size);
        assert(pb_decode(&stream, meshtastic_Data_fields, &decoded));
        assert(decoded.portnum == 303 && decoded.payload.size == sizeof(payload) &&
               !std::memcmp(decoded.payload.bytes, payload, sizeof(payload)));
        std::array<uint8_t, 255> shared{};
        size_t shared_size = shared.size();
        assert(mt::buildWirePacket(plain.data(), encrypted_size, header.from, header.id, header.to,
                                   header.channel, header.flags & 7, (header.flags & 8) != 0,
                                   channel_config.primary_key, key_size, shared.data(), &shared_size));
        assert(shared_size == radio.last_sent.size && !std::memcmp(shared.data(), radio.last_sent.data.data(), shared_size));
    }
    adapter.applyProtocolConfig(chat::MeshProtocol::Meshtastic, config);
    assert(adapter.getCapabilities().supports_protocol_ack_tracking);
    assert(!adapter.getCapabilities().supports_appdata_ack);
    std::vector<Result> results;
    adapter.setSendResultSink(result_sink, &results);
    const auto send = [&](uint32_t id, uint32_t destination = peer)
    {
        auto result = adapter.sendTextDetailed(chat::ChannelId::PRIMARY, "ACK test", id, destination);
        uint32_t sent_id;
        bool ok;
        while (adapter.takePendingSendResult(sent_id, ok))
        {
        }
        return result;
    };
    const auto receive = [&](runtime::RadioPacket packet)
    { radio.incoming.push_back(packet); adapter.tick(); };
    assert(send(0x2001).ok);
    receive(routing_packet(shared, 0x2002)); // Wrong request.
    receive(routing_packet(shared, 0x2001, meshtastic_Routing_Error_NONE, true, peer + 1));
    receive(routing_packet(shared, 0x2001, meshtastic_Routing_Error_NONE, true, peer, self + 1));
    receive(routing_packet(shared, 0x2001, meshtastic_Routing_Error_NONE, false, peer + 1));
    receive(routing_packet(shared, 0x2001, meshtastic_Routing_Error_NONE, true, peer, self, 0));
    receive(routing_packet(shared, 0x2001, meshtastic_Routing_Error_NONE, true, peer, self, 2));
    assert(results.empty());
    receive(routing_packet(shared, 0x2001, meshtastic_Routing_Error_NONE, false)); // Official shared-channel Routing ACK.
    assert(results.size() == 1 && results.back().id == 0x2001 && results.back().status == chat::MessageStatus::Delivered);
    receive(routing_packet(shared, 0x2001));
    assert(results.size() == 1);
    assert(send(0x2003).ok);
    receive(routing_packet(shared, 0x2003, meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY, false));
    assert(results.size() == 2 && results.back().status == chat::MessageStatus::Failed && results.back().failure == Failure::PeerKeyMissing);
    radio.tx_ok = false;
    assert(!send(0x2004).ok);
    radio.tx_ok = true;
    now_ms += 60000;
    adapter.tick();
    assert(results.size() == 2); // Failed physical TX must not reserve an ACK slot.
    for (uint32_t id = 1; id <= 32; ++id) assert(send(0x3000 + id).ok);
    auto transmissions = radio.transmissions;
    auto full = send(0x3033);
    assert(!full.ok && full.failure == chat::MeshOperationFailure::Busy && radio.transmissions == transmissions);
    auto duplicate = send(0x3001);
    assert(!duplicate.ok && duplicate.failure == chat::MeshOperationFailure::Busy && radio.transmissions == transmissions);
    now_ms += 59999;
    adapter.tick();
    assert(results.size() == 2);
    ++now_ms;
    adapter.tick();
    assert(results.size() == 34);
    for (size_t i = 2; i < results.size(); ++i) assert(results[i].failure == Failure::AckTimeout);
    now_ms = UINT32_MAX - 100;
    assert(send(0x4001).ok);
    now_ms += 59999;
    adapter.tick();
    assert(results.size() == 34);
    ++now_ms;
    adapter.tick();
    assert(results.size() == 35 && results.back().failure == Failure::AckTimeout);
    assert(send(0x4002).ok);
    adapter.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
    assert(results.size() == 36 && results.back().id == 0x4002 && results.back().failure == Failure::Rejected);
    adapter.applyProtocolConfig(chat::MeshProtocol::Meshtastic, config);
    receive(routing_packet(shared, 0x4002));
    assert(results.size() == 36);
    assert(send(0x4003, 0).ok); // Broadcast does not wait for peer confirmation.
    now_ms += 60000;
    adapter.tick();
    assert(results.size() == 36);
    assert(send(0x4004).ok);
    adapter.cancelPendingSends();
    assert(results.size() == 37 && results.back().failure == Failure::Rejected);
    receive(routing_packet(shared, 0x4004));
    assert(results.size() == 37);
    assert(send(0x4005).ok);
    auto corrupted = routing_packet(shared, 0x4005);
    corrupted.data[corrupted.size - 5] ^= 1;
    receive(corrupted);
    assert(results.size() == 37); // Invalid CCM tag cannot prove delivery.
    now_ms += 60000;
    adapter.tick();
    assert(results.size() == 38 && results.back().failure == Failure::AckTimeout);
    // The invalid-tag case intentionally discarded the peer key. Start the
    // NACK fixture with a known key, as for a cached-but-stale real peer.
    peer_key.node_id = mesh::NodeId{peer};
    assert(peer_store.put(peer_key).ok);
    Radio resync_radio;
    Adapter resync_adapter(resync_radio, self, clock_ms);
    resync_adapter.applyProtocolConfig(chat::MeshProtocol::Meshtastic, config);
    assert(resync_adapter.begin() && resync_adapter.hasPkiKey(peer));
    std::vector<Result> resync_results;
    resync_adapter.setSendResultSink(result_sink, &resync_results);
    for (auto reason : {meshtastic_Routing_Error_NO_CHANNEL, meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY})
    {
        const uint32_t id = 0x5000 + static_cast<uint32_t>(reason);
        assert(resync_adapter.sendTextDetailed(chat::ChannelId::PRIMARY, "stale key", id, peer).ok);
        const auto transmissions_before = resync_radio.transmissions;
        const auto results_before = resync_results.size();
        const auto nack = routing_packet(shared, id, reason, false);
        resync_radio.incoming.push_back(nack);
        resync_adapter.tick();
        assert(resync_results.size() == results_before + 1 && resync_results.back().status == chat::MessageStatus::Failed);
        assert(resync_radio.transmissions == transmissions_before + 1); // Request fresh NodeInfo after a matched NACK.
        resync_radio.incoming.push_back(nack);
        resync_adapter.tick();
        assert(resync_results.size() == results_before + 1 && resync_radio.transmissions == transmissions_before + 1);
    }
    resync_adapter.setSendResultSink(nullptr, nullptr);
    resync_adapter.suspendRadio();
    adapter.setSendResultSink(nullptr, nullptr);
    adapter.suspendRadio();
    std::filesystem::remove_all(root);
}
