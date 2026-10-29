#include "chat/linux_meshtastic_mqtt_codec.h"
#include "meshtastic/mqtt.pb.h"
#include <algorithm>
#include <cstring>
#include <pb_encode.h>
#include <stdexcept>
#include <vector>

namespace
{
using Codec = trailmate::linux_app::LinuxMeshtasticMqttCodec;
using Bytes = std::vector<uint8_t>;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
Bytes raw(const meshtastic_MeshPacket& packet, const char* channel = "primary", const char* gateway = "!00000001")
{
    Bytes bytes(512);
    meshtastic_ServiceEnvelope envelope{const_cast<meshtastic_MeshPacket*>(&packet), const_cast<char*>(channel), const_cast<char*>(gateway)};
    auto stream = pb_ostream_from_buffer(bytes.data(), bytes.size());
    require(pb_encode(&stream, meshtastic_ServiceEnvelope_fields, &envelope), "reference envelope encoding failed");
    bytes.resize(stream.bytes_written);
    return bytes;
}
} // namespace
int main()
{
    chat::meshtastic::MqttProxyRuntimeSettings settings;
    settings.enabled = settings.proxy_to_client_enabled = true;
    settings.encryption_enabled = false;
    settings.primary_uplink_enabled = settings.primary_downlink_enabled = true;
    settings.secondary_uplink_enabled = settings.secondary_downlink_enabled = true;
    settings.root = "fixture";
    settings.primary_channel_id = "primary";
    settings.secondary_channel_id = "secondary";
    Codec sender, receiver;
    require(sender.configure(settings, 1) && receiver.configure(settings, 2), "codec configuration failed");
    meshtastic_MeshPacket packet{};
    packet.from = 1;
    packet.to = 2;
    packet.id = 0x01020304;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    packet.decoded.payload.size = 2;
    packet.decoded.payload.bytes[0] = 'h';
    packet.decoded.payload.bytes[1] = 'i';
    Codec::Envelope envelope;
    require(sender.encode(packet, chat::ChannelId::PRIMARY, false, envelope), "text envelope failed");
    const Bytes golden{0x0A, 23, 0x0D, 1, 0, 0, 0, 0x15, 2, 0, 0, 0, 0x22, 6, 8, 1, 0x12, 2, 'h', 'i', 0x35, 4, 3, 2, 1,
                       0x12, 7, 'p', 'r', 'i', 'm', 'a', 'r', 'y', 0x1A, 9, '!', '0', '0', '0', '0', '0', '0', '0', '1'};
    require(envelope.size == golden.size() && std::equal(golden.begin(), golden.end(), envelope.bytes.begin()), "service envelope differs from literal protobuf vector");
    require(std::string(envelope.topic.data()) == "fixture/2/e/primary/!00000001", "wrong topic");
    Codec::Incoming incoming;
    require(receiver.decode(envelope.topic.data(), envelope.bytes.data(), envelope.size, true, incoming) && incoming.retained && !incoming.pki &&
                incoming.packet->decoded.payload.size == 2 && incoming.packet->decoded.payload.bytes[0] == 'h',
            "decoded payload metadata changed");
    const auto* received = incoming.packet;
    require(sender.encode(packet, chat::ChannelId::SECONDARY, false, envelope) && received->decoded.payload.bytes[0] == 'h', "reply encoding overwrote RX storage");
    require(receiver.decode(envelope.topic.data(), envelope.bytes.data(), envelope.size, false, incoming) && incoming.channel == chat::ChannelId::SECONDARY, "secondary mapping failed");
    packet.decoded.payload.size = sizeof(packet.decoded.payload.bytes);
    std::memset(packet.decoded.payload.bytes, 'x', sizeof(packet.decoded.payload.bytes));
    require(sender.encode(packet, chat::ChannelId::PRIMARY, false, envelope) && envelope.size > 255, "MQTT data was constrained to radio MTU");
    require(receiver.decode(envelope.topic.data(), envelope.bytes.data(), envelope.size, false, incoming) && incoming.packet->decoded.payload.size == 233, "full legal payload lost");
    packet.decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
    packet.decoded.payload.size = 3;
    packet.decoded.payload.bytes[0] = 1;
    packet.decoded.payload.bytes[1] = 0;
    packet.decoded.payload.bytes[2] = 2;
    require(sender.encode(packet, chat::ChannelId::PRIMARY, false, envelope) && receiver.decode(envelope.topic.data(), envelope.bytes.data(), envelope.size, false, incoming) &&
                incoming.packet->decoded.payload.bytes[1] == 0,
            "binary application payload changed");
    require(!sender.decode(envelope.topic.data(), envelope.bytes.data(), envelope.size, false, incoming), "own gateway echo admitted");
    require(!sender.encode(packet, chat::ChannelId::PRIMARY, true, envelope), "MQTT ingress was re-published");
    packet.via_mqtt = true;
    require(!sender.encode(packet, chat::ChannelId::PRIMARY, false, envelope), "packet MQTT flag was ignored");
    packet.via_mqtt = false;
    require(!sender.encode(packet, static_cast<chat::ChannelId>(255), false, envelope), "invalid channel accepted");
    auto input = raw(packet);
    require(!receiver.decode("fixture/2/e/secondary/!00000001", input.data(), input.size(), false, incoming), "topic/channel mismatch accepted");
    require(!receiver.decode("wrong/2/e/primary/!00000001", input.data(), input.size(), false, incoming), "wrong root accepted");
    require(!receiver.decode("fixture/2/e/primary/!00000002", input.data(), input.size(), false, incoming), "gateway mismatch accepted");
    input.insert(input.end(), golden.begin(), golden.end());
    require(!receiver.decode("fixture/2/e/primary/!00000001", input.data(), input.size(), false, incoming), "duplicate envelope fields accepted");
    input = raw(packet);
    input.push_back(0x20);
    input.push_back(1); // Unknown extension, safely skipped.
    require(receiver.decode("fixture/2/e/primary/!00000001", input.data(), input.size(), false, incoming), "unknown protobuf extension rejected");
    input = raw(packet, "primary", nullptr);
    require(receiver.decode("fixture/2/e/primary/!00000001", input.data(), input.size(), false, incoming), "legacy missing gateway rejected");
    require(!receiver.decode("fixture/2/e/primary/!00000002", input.data(), input.size(), false, incoming), "topic-inferred own gateway echo admitted");
    packet.from = 2;
    input = raw(packet);
    require(!receiver.decode("fixture/2/e/primary/!00000001", input.data(), input.size(), false, incoming), "own sender echo admitted");
    packet.from = 1;
    packet.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    input = raw(packet);
    require(!receiver.decode("fixture/2/e/primary/!00000001", input.data(), input.size(), false, incoming), "broker admin payload admitted");
    packet.decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
    auto encrypted_policy = settings;
    encrypted_policy.encryption_enabled = true;
    require(receiver.configure(encrypted_policy, 2), "encrypted policy failed");
    input = raw(packet);
    require(!receiver.decode("fixture/2/e/primary/!00000001", input.data(), input.size(), false, incoming), "plaintext bypassed encryption policy");
    packet.decoded.portnum = meshtastic_PortNum_MAP_REPORT_APP;
    input = raw(packet);
    require(receiver.decode("fixture/2/e/primary/!00000001", input.data(), input.size(), false, incoming), "metadata-only policy exception lost");
    require(receiver.configure(settings, 2), "restore policy failed");
    packet.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    packet.pki_encrypted = true;
    packet.encrypted.size = 3;
    packet.encrypted.bytes[0] = 1;
    packet.encrypted.bytes[1] = 0;
    packet.encrypted.bytes[2] = 2;
    require(sender.encode(packet, chat::ChannelId::PRIMARY, false, envelope) && std::string(envelope.topic.data()).find("/PKI/") != std::string::npos &&
                receiver.decode(envelope.topic.data(), envelope.bytes.data(), envelope.size, false, incoming) && incoming.pki && incoming.packet->encrypted.bytes[1] == 0,
            "PKI ciphertext was decoded or misrouted");
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    std::memset(&packet.decoded, 0, sizeof(packet.decoded));
    packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    input = raw(packet, "PKI");
    require(!receiver.decode("fixture/2/e/PKI/!00000001", input.data(), input.size(), false, incoming), "plaintext PKI claim admitted");
    packet.pki_encrypted = false;
    auto disabled = settings;
    disabled.primary_downlink_enabled = disabled.secondary_downlink_enabled = false;
    require(receiver.configure(disabled, 2), "disable downlink failed");
    input = raw(packet);
    require(!receiver.decode("fixture/2/e/primary/!00000001", input.data(), input.size(), false, incoming), "disabled downlink admitted data");
}
