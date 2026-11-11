#include "chat/infra/meshtastic/mt_codec_pb.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/linux_meshtastic_repeater.h"
#include "chat/linux_raw_lora_mesh_adapter.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <openssl/evp.h>
#include <unistd.h>

namespace mt = chat::meshtastic;
namespace runtime = platform::linux_runtime;
using trailmate::linux_app::meshtastic_wire::Repeater;
constexpr uint32_t self = 0xF48DA2AE, origin = 0x4BE5DDEF;
uint32_t now_ms = 100;
uint32_t clock_ms() { return now_ms; }

runtime::RadioPacket packet(uint32_t id = 1)
{
    runtime::RadioPacket result{};
    mt::PacketHeaderWire header{};
    header.from = origin;
    header.to = 0xffffffff;
    header.id = id;
    header.channel = 8;
    header.flags = 3 | (3 << mt::PACKET_FLAGS_HOP_START_SHIFT) | mt::PACKET_FLAGS_WANT_ACK_MASK;
    std::memcpy(result.data.data(), &header, sizeof(header));
    for (size_t i = sizeof(header); i < 100; ++i) result.data[i] = uint8_t(i * 37);
    result.size = 100;
    return result;
}
mt::PacketHeaderWire header(const uint8_t* bytes)
{
    mt::PacketHeaderWire result;
    std::memcpy(&result, bytes, sizeof(result));
    return result;
}

runtime::RadioPacket application_packet(uint32_t id, uint32_t to = 0xffffffff)
{
    uint8_t payload[] = {1, 2, 3}, encoded[128], key[32], nonce[16]{};
    size_t size = sizeof(encoded), key_size = 0;
    assert(mt::encodeAppData(300, payload, sizeof(payload), false, encoded, &size));
    mt::expandShortPsk(1, key, &key_size);
    runtime::RadioPacket result{};
    result.size = result.data.size();
    assert(mt::buildWirePacket(encoded, size, origin, id, to, 8, 2, false, nullptr, 0, result.data.data(), &result.size));
    const uint64_t wide_id = id;
    std::memcpy(nonce, &wide_id, sizeof(wide_id));
    std::memcpy(nonce + 8, &origin, sizeof(origin));
    auto* cipher = EVP_CIPHER_CTX_new();
    int count = 0, tail = 0;
    assert(cipher && key_size == 16 && EVP_EncryptInit_ex(cipher, EVP_aes_128_ctr(), nullptr, key, nonce) == 1 &&
           EVP_EncryptUpdate(cipher, result.data.data() + sizeof(mt::PacketHeaderWire), &count, encoded, size) == 1 &&
           EVP_EncryptFinal_ex(cipher, result.data.data() + sizeof(mt::PacketHeaderWire) + count, &tail) == 1 && size_t(count + tail) == size);
    EVP_CIPHER_CTX_free(cipher);
    return result;
}

struct Radio final : runtime::IPacketRadio
{
    bool online = false;
    runtime::RadioSettings settings{};
    std::deque<runtime::RadioPacket> incoming;
    std::vector<Repeater::Frame> sent;
    const char* name() const override { return "relay test radio"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override { return online = true; }
    void release() override { online = false; }
    bool configureLoRa(const runtime::RadioSettings& value) override
    {
        settings = value;
        return true;
    }
    bool startReceive() override { return online; }
    bool transmit(const uint8_t* bytes, size_t size) override
    {
        Repeater::Frame frame;
        assert(size <= frame.bytes.size());
        frame.size = size;
        std::memcpy(frame.bytes.data(), bytes, size);
        sent.push_back(frame);
        return true;
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
    const char* lastError() const override { return ""; }
    runtime::RadioSettings appliedLoRaConfig() const override { return settings; }
    runtime::RadioStatistics stats() const override { return {}; }
};

int main()
{
    Repeater relay;
    Repeater::Frame frame;
    auto original = packet();
    assert(relay.receive(self, original, 8, 8, 100, 0));
    assert(!relay.receive(self, original, 8, 8, 110, 0));
    assert(!relay.take(499, frame) && relay.take(500, frame));
    const auto before = header(original.data.data()), after = header(frame.bytes.data());
    assert(after.from == before.from && after.to == before.to && after.id == before.id && after.channel == before.channel);
    assert((after.flags & mt::PACKET_FLAGS_HOP_LIMIT_MASK) == 2);
    assert((after.flags & ~mt::PACKET_FLAGS_HOP_LIMIT_MASK) == (before.flags & ~mt::PACKET_FLAGS_HOP_LIMIT_MASK));
    assert(after.relay_node == uint8_t(self) && after.next_hop == 0 && frame.size == original.size);
    assert(!std::memcmp(frame.bytes.data() + sizeof(after), original.data.data() + sizeof(before), original.size - sizeof(before)));
    relay.complete(frame, 500, true);
    assert(relay.queued() == 0 && !relay.receive(self, original, 8, 8, 3499, 0));
    assert(relay.receive(self, original, 8, 8, 3500, 0) && relay.take(3900, frame));
    relay.complete(frame, 3900, true);
    assert(relay.receive(self, original, 8, 8, 7000, 0) && relay.take(7400, frame));
    relay.complete(frame, 7400, true);
    assert(!relay.receive(self, original, 8, 8, 11000, 0));
    // A relayed duplicate cannot start another loop, even after the retry interval.
    auto repeated = original;
    std::memcpy(repeated.data.data(), &after, sizeof(after));
    assert(!relay.receive(self, repeated, 8, 8, 12000, 0));
    relay.reset();
    for (unsigned variant = 0; variant < 7; ++variant)
    {
        auto rejected = original;
        auto h = before;
        if (variant == 0) h.from = self;
        if (variant == 1) h.to = self;
        if (variant == 2) h.id = 0;
        if (variant == 3) h.flags &= ~mt::PACKET_FLAGS_HOP_LIMIT_MASK;
        if (variant == 4) h.channel = 9;
        if (variant == 5) h.next_hop = uint8_t(self) + 1;
        if (variant == 6) rejected.size = sizeof(h);
        std::memcpy(rejected.data.data(), &h, sizeof(h));
        assert(!relay.receive(self, rejected, 8, 8, 100, 0));
    }
    for (unsigned i = 0; i < 8; ++i) assert(relay.receive(self, packet(100 + i), 8, 8, 100, i));
    assert(relay.queued() == 8 && !relay.receive(self, packet(108), 8, 8, 100, 0));
    assert(!relay.take(15100, frame) && relay.queued() == 0);
    // Monotonic wrap and bounded TX failure retry.
    assert(relay.receive(self, original, 8, 8, 0xffffff00, 0));
    assert(!relay.take(0x80, frame) && relay.take(0x90, frame));
    relay.complete(frame, 0x90, false);
    assert(!relay.take(0x477, frame) && relay.take(0x478, frame));
    relay.complete(frame, 0x478, false);
    assert(relay.take(0x860, frame));
    relay.complete(frame, 0x860, false);
    assert(relay.queued() == 0);

    // Exercise the real physical-RX adapter integration, with relay opt-in and reset.
    char root[] = "/tmp/trailmate-relay-XXXXXX";
    assert(mkdtemp(root));
    setenv("TRAIL_MATE_SETTINGS_ROOT", root, 1);
    setenv("XDG_STATE_HOME", root, 1);
    {
        Radio radio;
        trailmate::linux_app::LinuxRawLoraMeshAdapter adapter(radio, self, clock_ms);
        chat::MeshConfig config{};
        config.enable_relay = false;
        adapter.applyProtocolConfig(chat::MeshProtocol::Meshtastic, config);
        assert(adapter.begin());
        radio.incoming.push_back(original);
        adapter.tick();
        now_ms = 2000;
        adapter.tick();
        assert(radio.sent.empty());
        config.enable_relay = true;
        adapter.applyProtocolConfig(chat::MeshProtocol::Meshtastic, config);
        radio.incoming.push_back(original);
        adapter.tick();
        now_ms = 4000;
        adapter.tick();
        assert(radio.sent.size() == 1 && radio.sent.back().bytes == frame.bytes);
        // This queued frame must disappear when the user disables relaying.
        radio.incoming.push_back(packet(2));
        adapter.tick();
        config.enable_relay = false;
        adapter.applyProtocolConfig(chat::MeshProtocol::Meshtastic, config);
        now_ms = 4500;
        adapter.tick();
        assert(radio.sent.size() == 1);
        chat::MeshIncomingData incoming;
        const auto app = application_packet(0x4567);
        radio.incoming.push_back(app);
        radio.incoming.push_back(app);
        adapter.tick();
        assert(adapter.pollIncomingData(&incoming) && incoming.packet_id == 0x4567 && incoming.payload.size() == 3);
        assert(!adapter.pollIncomingData(&incoming));
        radio.incoming.push_back(application_packet(0x4568, self + 1));
        adapter.tick();
        assert(!adapter.pollIncomingData(&incoming));
    }
    std::filesystem::remove_all(root);
}
