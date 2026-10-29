#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/linux_meshcore_advert_wire.h"
#include "chat/linux_raw_lora_mesh_adapter.h"
#include "sys/clock.h"
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <stdexcept>
#include <unistd.h>

namespace
{
namespace port = platform::linux_runtime;
namespace wire = trailmate::linux_app::meshcore_wire;
namespace mc = chat::meshcore;
uint32_t current_time = 1000;
uint32_t now() { return current_time; }
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
struct Radio final : port::IPacketRadio
{
    std::deque<port::RadioPacket> incoming;
    std::vector<port::RadioPacket> sent;
    port::RadioSettings settings;
    bool online = false;
    const char* name() const override { return "repeater fake radio"; }
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
        port::RadioPacket packet;
        require(size <= packet.data.size(), "oversized TX");
        packet.size = size;
        std::memcpy(packet.data.data(), bytes, size);
        sent.push_back(packet);
        return online;
    }
    bool pollReceive(port::RadioPacket* packet) override
    {
        if (incoming.empty()) return false;
        *packet = incoming.front();
        incoming.pop_front();
        return true;
    }
    float readRssi() override { return -100; }
    bool isOnline() const override { return online; }
    const char* lastError() const override { return "injected radio"; }
    port::RadioSettings appliedLoRaConfig() const override { return settings; }
    port::RadioStatistics stats() const override { return {}; }
};
port::RadioPacket frame(uint8_t type, const uint8_t* bytes, size_t size, uint8_t route = 1)
{
    port::RadioPacket packet;
    require(mc::buildFrameNoTransport(mc::PayloadProfile::V2, route, type, nullptr, 0, bytes, size,
                                      packet.data.data(), packet.data.size(), &packet.size),
            "fixture frame failed");
    packet.snr_db = -30;
    return packet;
}
mc::ParsedPacket parse(const port::RadioPacket& packet)
{
    mc::ParsedPacket parsed;
    require(mc::parsePacket(packet.data.data(), packet.size, &parsed), "invalid emitted frame");
    return parsed;
}
} // namespace

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-repeater-adapter-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", root.c_str(), 1);
    sys::set_millis_provider(now);
    {
        Radio radio;
        trailmate::linux_app::LinuxRawLoraMeshAdapter adapter(radio, 123, now);
        chat::MeshConfig config;
        config.tx_enabled = true;
        config.meshcore_rx_delay_base = 0;
        config.meshcore_airtime_factor = 0;
        config.meshcore_sf = 7;
        config.meshcore_bw_khz = 125;
        for (unsigned index = 0; index < 16; ++index) config.meshcore_channels[0].key[index] = index;
        adapter.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
        require(adapter.isReady(), "adapter not ready");
        port::LinuxMeshCoreIdentity identity;
        require(identity.begin(), "identity unavailable");
        const uint8_t opaque[]{0x99, 1, 2, 3};
        auto incoming = frame(6, opaque, sizeof(opaque));
        radio.incoming.push_back(incoming);
        adapter.tick();
        require(radio.sent.empty(), "disabled repeater transmitted");
        require(adapter.requestNodeInfo(0, false), "chat advertisement failed");
        mesh::MeshProtocolEvent advert;
        require(wire::decodeAdvert(radio.sent.back().data.data(), radio.sent.back().size, advert) && advert.advert.node_type == mc::kMeshCoreAdvertTypeChat,
                "disabled repeater advertised forwarding");
        config.meshcore_client_repeat = true;
        adapter.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
        require(adapter.requestNodeInfo(0, false), "repeater advertisement failed");
        require(wire::decodeAdvert(radio.sent.back().data.data(), radio.sent.back().size, advert) && advert.advert.node_type == mc::kMeshCoreAdvertTypeRepeater,
                "enabled forwarding was not advertised");
        size_t before = radio.sent.size();
        radio.incoming.push_back(incoming);
        adapter.tick();
        require(radio.sent.size() == before + 1, "worker did not transmit queued flood");
        const auto parsed = parse(radio.sent.back());
        require(parsed.path_len == 2 && !std::memcmp(parsed.path, identity.publicKey().data(), 2), "worker forwarded wrong path");
        radio.incoming.push_back(incoming);
        adapter.tick();
        require(radio.sent.size() == before + 1, "worker repeated duplicate flood");
        chat::MessageId message = 0;
        require(adapter.sendText(chat::ChannelId::PRIMARY, "own relay echo", &message), "own group TX failed");
        const auto own = radio.sent.back();
        before = radio.sent.size();
        radio.incoming.push_back(own);
        adapter.tick();
        require(radio.sent.size() == before, "outgoing message was repeated on echo");
        config.meshcore_rx_delay_base = 10;
        adapter.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
        uint8_t nonce = 41;
        radio.incoming.push_back(frame(6, &nonce, 1));
        adapter.tick();
        require(radio.sent.size() == before, "forward delay was ignored");
        config.meshcore_client_repeat = false;
        adapter.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
        current_time += 1000;
        adapter.tick();
        require(radio.sent.size() == before, "disable did not cancel pending forwarding");
        config.meshcore_client_repeat = true;
        adapter.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
        ++nonce;
        radio.incoming.push_back(frame(6, &nonce, 1));
        adapter.tick();
        config.meshcore_freq_mhz += 0.1f;
        adapter.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
        current_time += 1000;
        adapter.tick();
        require(radio.sent.size() == before, "retune did not cancel pending forwarding");
        mc::MeshCoreDiscoverRequestBuildInfo request;
        request.tag = 0x01020304;
        request.type_filter = 1 << mc::kMeshCoreAdvertTypeRepeater;
        uint8_t payload[10]{};
        size_t size = 0;
        require(mc::buildDiscoverRequestControlPayload(request, payload, sizeof(payload), &size), "discovery fixture failed");
        radio.incoming.push_back(frame(mc::kMeshCorePayloadTypeControl, payload, size, 2));
        adapter.tick();
        current_time += 2100;
        adapter.tick();
        require(radio.sent.size() == before + 1, "repeater-only discovery filter was ignored");
        const auto response_frame = parse(radio.sent.back());
        mc::DecodedDiscoverResponse response;
        require(mc::decodeDiscoverResponse(response_frame.payload, response_frame.payload_len, &response) && response.node_type == mc::kMeshCoreAdvertTypeRepeater,
                "discovery response used wrong role");
        before = radio.sent.size();
        ++request.tag;
        require(mc::buildDiscoverRequestControlPayload(request, payload, sizeof(payload), &size), "flood discovery fixture failed");
        radio.incoming.push_back(frame(mc::kMeshCorePayloadTypeControl, payload, size, 1));
        adapter.tick();
        current_time += 2100;
        adapter.tick();
        require(radio.sent.size() == before + 1, "zero-hop flood discovery compatibility missing");
        wire::Discovery invalid_server;
        auto invalid_request = frame(mc::kMeshCorePayloadTypeControl, payload, size, 2);
        invalid_request.data[0] |= 0x80; // Reserved payload version 3.
        invalid_server.receive(config, identity.publicKey(), invalid_request, current_time, 1700000000, 0, nullptr);
        wire::Frame invalid_response;
        require(!invalid_server.takeResponse(current_time + 2100, invalid_response), "reserved payload version accepted");
    }
    sys::set_millis_provider(nullptr);
    std::filesystem::remove_all(root);
}
