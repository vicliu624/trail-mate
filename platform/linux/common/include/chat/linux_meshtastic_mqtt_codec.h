#pragma once
#include "chat/infra/meshtastic/mt_mqtt_proxy_runtime.h"
#include "meshtastic/mesh.pb.h"
#include <array>
#include <string_view>

namespace trailmate::linux_app
{
// Protocol/application boundary only; no socket, radio API or packet-MTU conversion.
class LinuxMeshtasticMqttCodec
{
  public:
    struct Envelope
    {
        std::array<char, 193> topic{};
        std::array<uint8_t, 512> bytes{};
        size_t size = 0;
    };
    struct Incoming
    {
        // Owned by this codec, valid until the next decode/configure. Encoding
        // does not overwrite RX storage, including replies during RX handling.
        const meshtastic_MeshPacket* packet = nullptr;
        chat::ChannelId channel = chat::ChannelId::PRIMARY;
        // PKI is a channel/ciphertext selector, never an authentication verdict.
        bool pki = false, retained = false;
    };
    bool configure(const chat::meshtastic::MqttProxyRuntimeSettings&, chat::NodeId self);
    bool encode(const meshtastic_MeshPacket&, chat::ChannelId, bool from_mqtt, Envelope&);
    bool decode(std::string_view topic, const uint8_t*, size_t, bool retained, Incoming&);

  private:
    chat::meshtastic::MqttProxyRuntimeSettings settings_;
    chat::NodeId self_ = 0;
    std::array<char, 16> gateway_{};
    meshtastic_MeshPacket incoming_{};
    std::array<char, 65> channel_{}, received_gateway_{};
};
} // namespace trailmate::linux_app
