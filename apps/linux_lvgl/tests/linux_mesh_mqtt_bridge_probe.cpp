#include "chat/linux_mesh_mqtt_bridge.h"
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv)
{
    if (argc != 4) return 2;
    using Bridge = trailmate::linux_app::LinuxMeshMqttBridge;
    const std::string mode = argv[3];
    Bridge bridge;
    Bridge::Config config;
    config.client.enabled = true;
    config.client.host = argv[1];
    config.client.port = static_cast<uint16_t>(std::stoul(argv[2]));
    config.client.client_id = "tm-fixture";
    config.client.username = "u";
    config.client.password = "p";
    config.client.keepalive_seconds = 1;
    config.client.connect_ms = 500;
    config.client.handshake_ms = 500;
    config.client.pending_ms = 500;
    config.client.retry_initial_ms = 50;
    config.client.retry_max_ms = 100;
    config.root = "fixture";
    config.protocol = mode == "mc" ? chat::MeshProtocol::MeshCore : chat::MeshProtocol::Meshtastic;
    config.meshtastic.primary_channel_id = "primary";
    config.meshtastic.primary_uplink_enabled = config.meshtastic.primary_downlink_enabled = true;
    config.meshtastic.encryption_enabled = false;
    require(bridge.configure(config, 1) && bridge.active() && !bridge.ready(), "bridge prematurely ready");
    const auto configured_generation = bridge.snapshot().generation;
    const auto start = std::chrono::steady_clock::now();
    bool received_mt = false, received_vmp = false;
    Bridge::Incoming incoming;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(4))
    {
        bridge.tick();
        if (bridge.ready())
        {
            require(bridge.snapshot().generation > configured_generation, "connection did not advance bridge generation");
            if (mode == "cancel")
            {
                bridge.setCancellation([]
                                       { return true; });
                meshtastic_MeshPacket packet{};
                packet.from = 1;
                packet.to = 2;
                packet.id = 7;
                packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
                packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
                require(!bridge.publishMeshtastic(packet, chat::ChannelId::PRIMARY), "cancelled owner published");
                const auto previous = bridge.snapshot().generation;
                auto invalid = config;
                invalid.client.host = "wrong/host";
                require(!bridge.configure(invalid, 1) && bridge.active() && !bridge.ready() && bridge.snapshot().generation > previous && bridge.snapshot().error[0],
                        "invalid requested config fell back or reused old cohort");
                bridge.clear();
                require(!bridge.active(), "clear left MQTT selected");
                return 0;
            }
            if (bridge.receive(incoming))
            {
                require(incoming.generation == bridge.snapshot().generation, "wrong receive cohort");
                if (mode == "mc")
                {
                    const uint8_t expected[]{0x59, 0x40, 0xAA, 0xBB, 0xCC};
                    require(incoming.kind == Bridge::Incoming::Kind::MeshCore && incoming.meshcore.size == sizeof(expected) &&
                                !std::memcmp(incoming.meshcore.bytes.data(), expected, sizeof(expected)),
                            "MC raw carrier changed or retained echo admitted");
                    require(!bridge.publishMeshCore(expected, sizeof(expected), true), "MC broker input re-published");
                    require(bridge.publishMeshCore(expected, sizeof(expected)), "MC completed write failed");
                    require(!bridge.publishVmp(expected, sizeof(expected)), "VMP entered MC carrier");
                    bridge.clear();
                    return 0;
                }
                if (incoming.kind == Bridge::Incoming::Kind::Meshtastic)
                {
                    require(!received_mt && incoming.meshtastic.packet->decoded.payload.size == 233 && incoming.meshtastic.packet->decoded.payload.bytes[0] == 'x', "MT payload was truncated or misrouted");
                    const auto* borrowed = incoming.meshtastic.packet;
                    meshtastic_MeshPacket packet{};
                    packet.from = 1;
                    packet.to = 2;
                    packet.id = 7;
                    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
                    packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
                    packet.decoded.payload.size = 3;
                    std::memcpy(packet.decoded.payload.bytes, "out", 3);
                    require(bridge.publishMeshtastic(packet, chat::ChannelId::PRIMARY) && borrowed->decoded.payload.size == 233 && borrowed->decoded.payload.bytes[0] == 'x',
                            "reply write failed or overwrote in-flight MT RX");
                    received_mt = true;
                }
                else
                {
                    constexpr std::string_view expected = "vmp-carrier";
                    require(incoming.kind == Bridge::Incoming::Kind::Vmp && incoming.vmp_size == expected.size() && !std::memcmp(incoming.vmp.data(), expected.data(), expected.size()), "VMP topic reached MT decoder");
                    received_vmp = true;
                }
                if (received_mt && received_vmp)
                {
                    bridge.clear();
                    return 0;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("MQTT bridge loopback timed out");
}
