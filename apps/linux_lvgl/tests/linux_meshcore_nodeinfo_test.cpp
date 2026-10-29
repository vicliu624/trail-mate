#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/linux_meshcore_direct_wire.h"
#include "chat/linux_raw_lora_mesh_adapter.h"
#include "sys/event_bus.h"
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <stdexcept>
#include <unistd.h>

namespace wire = trailmate::linux_app::meshcore_wire;
namespace mc = chat::meshcore;
namespace port = platform::linux_runtime;
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
    const char* name() const override { return "nodeinfo fixture"; }
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
        require(size <= packet.data.size(), "oversized frame");
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
    const char* lastError() const override { return "fixture"; }
    port::RadioSettings appliedLoRaConfig() const override { return settings; }
    port::RadioStatistics stats() const override { return {}; }
};
void drain()
{
    sys::Event* event = nullptr;
    while (sys::EventBus::subscribe(&event)) delete event;
}
void wireCases(chat::MeshConfig config)
{
    const auto profile = config.meshcore_send_profile == chat::MeshCorePayloadSendProfile::V1Only ? mc::PayloadProfile::V1 : mc::PayloadProfile::V2;
    mc::MeshCoreNodeInfoBuildInfo info;
    info.node_id = 123;
    info.timestamp = 1700000000;
    info.short_name = "S";
    info.long_name = "Long name";
    uint8_t payload[mc::kMeshCoreNodeInfoInfoPayloadSize]{};
    size_t size = 0;
    require(mc::buildNodeInfoInfoControlPayload(info, payload, sizeof(payload), &size) && payload[size - 1] == 0, "invalid fixed fixture");
    wire::Frame frame;
    require(wire::encodeData(config, chat::ChannelId::PRIMARY, 123, 4, payload, size, frame), "zero-tail info rejected");
    wire::Incoming decoded;
    require(wire::decode(config, frame.bytes.data(), frame.size, decoded) && decoded.size == size && !std::memcmp(decoded.payload.data(), payload, size), "group info zero tail lost");
    mc::DecodedNodeInfoControl control;
    require(mc::decodeNodeInfoControlPayload(decoded.payload.data(), decoded.size, &control) && control.complete && std::string(control.info.long_name) == "Long name", "incomplete info");
    require(!wire::encodeData(config, chat::ChannelId::PRIMARY, 123, 123, payload, size, frame), "arbitrary zero-tail payload accepted");
    uint8_t padded[96]{};
    padded[0] = 'G';
    padded[1] = 'D';
    padded[2] = 123;
    padded[6] = 4;
    std::memcpy(padded + 10, payload, size);
    require(mc::trimGroupPlaintextPadding(padded, sizeof(padded)) == 10 + size, "group fixed trim failed");
    padded[10 + size] = 1;
    require(mc::trimGroupPlaintextPadding(padded, sizeof(padded)) == 0, "group nonzero padding accepted");
    std::memset(padded, 0, sizeof(padded));
    padded[0] = 0xDA;
    padded[1] = 0x7A;
    padded[3] = 4;
    std::memcpy(padded + 7, payload, size);
    require(mc::trimPeerPlaintextPadding(7, padded, sizeof(padded)) == 7 + size, "private fixed trim failed");
    padded[7 + size] = 1;
    require(mc::trimPeerPlaintextPadding(7, padded, sizeof(padded)) == 0, "private nonzero padding accepted");
    (void)profile;
}
int main()
{
    char directory[] = "/tmp/trailmate-nodeinfo-XXXXXX";
    require(mkdtemp(directory), "temporary root failed");
    const std::filesystem::path root(directory);
    sys::EventBus::init();
    for (auto profile : {chat::MeshCorePayloadSendProfile::V1Only, chat::MeshCorePayloadSendProfile::V2Only})
    {
        chat::MeshConfig config;
        config.tx_enabled = true;
        config.meshcore_send_profile = profile;
        wireCases(config);
        Radio ar, br;
        trailmate::linux_app::LinuxRawLoraMeshAdapter a(ar, 11), b(br, 22);
        const auto base = root / (profile == chat::MeshCorePayloadSendProfile::V1Only ? "v1" : "v2");
        setenv("TRAIL_MATE_SETTINGS_ROOT", (base / "a").c_str(), 1);
        a.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
        wire::Identity ak;
        require(ak.begin(), "sender identity missing");
        a.setUserInfo("Alpha full", "A");
        setenv("TRAIL_MATE_SETTINGS_ROOT", (base / "b").c_str(), 1);
        b.applyProtocolConfig(chat::MeshProtocol::MeshCore, config);
        wire::Identity bk;
        require(bk.begin(), "receiver identity missing");
        b.setUserInfo("Beta full", "B");
        require(a.requestNodeInfo(0, false) && b.requestNodeInfo(0, false), "advert failed");
        ar.incoming.push_back(br.sent.back());
        br.incoming.push_back(ar.sent.back());
        setenv("TRAIL_MATE_SETTINGS_ROOT", (base / "a").c_str(), 1);
        a.tick();
        setenv("TRAIL_MATE_SETTINGS_ROOT", (base / "b").c_str(), 1);
        b.tick();
        drain();
        require(a.getCapabilities().supports_node_info_query && a.getCapabilities().supports_node_info_reply, "capabilities missing");
        require(a.requestNodeInfo(b.getNodeId(), true), "private query rejected");
        const auto query = ar.sent.back();
        const auto before = br.sent.size();
        br.incoming.push_back(query);
        b.tick();
        require(br.sent.size() == before + 1, "query did not produce one info response");
        const auto reply = br.sent.back();
        br.incoming.push_back(query);
        b.tick();
        require(br.sent.size() == before + 1, "duplicate query amplified");
        ar.incoming.push_back(reply);
        a.tick();
        sys::Event* event = nullptr;
        require(sys::EventBus::subscribe(&event) && event->type == sys::EventType::NodeInfoUpdate, "info event missing");
        auto* info = static_cast<sys::NodeInfoUpdateEvent*>(event);
        require(info->node_id == b.getNodeId() && std::string(info->short_name) == "B" && std::string(info->long_name) == "Beta full", "info names or peer binding lost");
        require(!info->has_public_key_state, "app info changed key trust");
        delete event;
        mc::MeshCoreNodeInfoBuildInfo spoof;
        spoof.node_id = b.getNodeId() ^ 0x01000000;
        spoof.short_name = "forged";
        spoof.long_name = "wrong authenticated peer";
        uint8_t spoof_payload[mc::kMeshCoreNodeInfoInfoPayloadSize]{};
        size_t spoof_size = 0;
        require(mc::buildNodeInfoInfoControlPayload(spoof, spoof_payload, sizeof(spoof_payload), &spoof_size), "spoof fixture failed");
        wire::Frame forged;
        uint32_t ack = 0;
        const auto wire_profile = profile == chat::MeshCorePayloadSendProfile::V1Only ? mc::PayloadProfile::V1 : mc::PayloadProfile::V2;
        require(wire::encodeDirectData(bk, ak.publicKey(), wire_profile, 4, spoof_payload, spoof_size, false, forged, ack), "spoof encryption failed");
        port::RadioPacket forged_packet;
        forged_packet.size = forged.size;
        std::memcpy(forged_packet.data.data(), forged.bytes.data(), forged.size);
        ar.incoming.push_back(forged_packet);
        a.tick();
        require(!sys::EventBus::subscribe(&event), "authenticated peer renamed a different node");
        chat::MeshIncomingData leaked;
        require(!a.pollIncomingData(&leaked) && !b.pollIncomingData(&leaked), "control leaked into app inbox");
        require(a.requestNodeInfo(0, true), "broadcast query rejected");
        br.incoming.push_back(ar.sent.back());
        b.tick();
        require(br.sent.size() == before + 2, "broadcast query did not reply");
        auto disabled = config;
        disabled.tx_enabled = false;
        b.applyProtocolConfig(chat::MeshProtocol::MeshCore, disabled);
        require(!b.requestNodeInfo(a.getNodeId(), true), "disabled TX admitted query");
        drain();
    }
    std::filesystem::remove_all(root);
}
