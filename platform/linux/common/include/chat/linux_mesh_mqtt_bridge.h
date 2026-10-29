#pragma once
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/linux_meshcore_group_wire.h"
#include "chat/linux_meshtastic_mqtt_codec.h"
#include "chat/linux_mqtt_client.h"
#include <functional>

namespace trailmate::linux_app
{
// Application protocol-worker owner. This class has no radio dependency.
class LinuxMeshMqttBridge
{
  public:
    struct Config
    {
        chat::MeshProtocol protocol = chat::MeshProtocol::Meshtastic;
        LinuxMqttClient::Options client;
        std::string root;
        bool uplink = true, downlink = true;
        chat::meshtastic::MqttProxyRuntimeSettings meshtastic;
    };
    struct Incoming
    {
        enum class Kind
        {
            Meshtastic,
            MeshCore,
            Vmp
        } kind = Kind::Meshtastic;
        uint64_t generation = 0;
        LinuxMeshtasticMqttCodec::Incoming meshtastic;
        meshcore_wire::Frame meshcore;
        std::array<uint8_t, 512> vmp{};
        size_t vmp_size = 0;
    };
    using Clock = uint32_t (*)();
    explicit LinuxMeshMqttBridge(Clock = nullptr);
    ~LinuxMeshMqttBridge();
    bool configure(const Config&, chat::NodeId self);
    void tick();
    void clear();
    // Owner teardown cancellation; called only on the serialized worker.
    void setCancellation(std::function<bool()>);
    bool active() const;
    bool ready() const;
    LinuxMqttClient::Snapshot snapshot() const;
    bool receive(Incoming&);
    // Synchronous bounded worker-only completion, matching the existing mesh
    // engine's send contract. A true result means a complete socket write.
    bool publishMeshtastic(const meshtastic_MeshPacket&, chat::ChannelId, bool from_mqtt = false);
    bool publishMeshtasticPayload(const chat::meshtastic::PacketHeaderWire&, chat::ChannelId,
                                  const uint8_t*, size_t, bool pki_ciphertext);
    bool publishMeshCore(const uint8_t*, size_t, bool from_mqtt = false);
    bool publishVmp(const uint8_t*, size_t);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace trailmate::linux_app
