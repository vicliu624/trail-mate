#include "chat/linux_meshtastic_mqtt_codec.h"
#include "chat/infra/mqtt/mqtt311_codec.h"
#include "chat/runtime/meshtastic_protocol_policy.h"
#include "meshtastic/mqtt.pb.h"
#include <cstdio>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>

namespace trailmate::linux_app
{
namespace
{
namespace mt = chat::meshtastic;
bool channelName(const std::string& value, bool required)
{
    return (!required && value.empty()) || (!value.empty() && value.size() <= 64 && chat::mqtt311::validTopic(value, false));
}
bool variant(const meshtastic_MeshPacket& packet)
{
    if (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag)
        return !packet.pki_encrypted && packet.decoded.payload.size <= sizeof(packet.decoded.payload.bytes);
    return packet.which_payload_variant == meshtastic_MeshPacket_encrypted_tag &&
           packet.encrypted.size && packet.encrypted.size <= sizeof(packet.encrypted.bytes);
}
bool readString(pb_istream_t& stream, char* output, size_t capacity)
{
    pb_istream_t substream;
    if (!pb_make_string_substream(&stream, &substream)) return false;
    const size_t size = substream.bytes_left;
    if (size >= capacity || !pb_read(&substream, reinterpret_cast<pb_byte_t*>(output), size)) return false;
    output[size] = 0;
    return chat::mqtt311::validText({output, size}) && pb_close_string_substream(&stream, &substream);
}
} // namespace
bool LinuxMeshtasticMqttCodec::configure(const mt::MqttProxyRuntimeSettings& settings, chat::NodeId self)
{
    if (!self || self == 0xFFFFFFFFU || settings.root.empty() || settings.root.size() > 64 || !chat::mqtt311::validTopic(settings.root, false) ||
        !channelName(settings.primary_channel_id, settings.primary_uplink_enabled || settings.primary_downlink_enabled) ||
        !channelName(settings.secondary_channel_id, settings.secondary_uplink_enabled || settings.secondary_downlink_enabled)) return false;
    settings_ = settings;
    self_ = self;
    std::snprintf(gateway_.data(), gateway_.size(), "!%08lx", static_cast<unsigned long>(self));
    std::memset(&incoming_, 0, sizeof(incoming_));
    channel_.fill(0);
    received_gateway_.fill(0);
    return true;
}
bool LinuxMeshtasticMqttCodec::encode(const meshtastic_MeshPacket& packet, chat::ChannelId channel, bool from_mqtt, Envelope& output)
{
    output.size = 0;
    output.topic.fill(0);
    if (!self_ || !variant(packet) || !packet.from || packet.from == 0xFFFFFFFFU ||
        static_cast<uint8_t>(channel) >= chat::kPrimarySecondaryChannelMaxCount) return false;
    const bool pki = packet.pki_encrypted;
    if (!mt::shouldPublishToMqtt(settings_, channel, from_mqtt || packet.via_mqtt, pki)) return false;
    const char* identifier = pki ? "PKI" : mt::mqttChannelIdFor(settings_, channel);
    if (!identifier || !*identifier) return false;
    if (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        (packet.decoded.portnum == meshtastic_PortNum_ADMIN_APP || (settings_.encryption_enabled && !mt::isMqttMetadataOnlyDownlinkPort(packet.decoded.portnum)))) return false;
    const int length = std::snprintf(output.topic.data(), output.topic.size(), "%s/2/e/%s/%s", settings_.root.c_str(), identifier, gateway_.data());
    if (length <= 0 || size_t(length) >= output.topic.size() || !chat::mqtt311::validTopic(output.topic.data(), false)) return false;
    meshtastic_ServiceEnvelope envelope{const_cast<meshtastic_MeshPacket*>(&packet), const_cast<char*>(identifier), gateway_.data()};
    auto stream = pb_ostream_from_buffer(output.bytes.data(), output.bytes.size());
    if (!pb_encode(&stream, meshtastic_ServiceEnvelope_fields, &envelope)) return false;
    output.size = stream.bytes_written;
    return true;
}
bool LinuxMeshtasticMqttCodec::decode(std::string_view topic, const uint8_t* bytes, size_t size, bool retained, Incoming& output)
{
    output = {};
    if (!self_ || !mt::mqttProxyRuntimeEnabled(settings_) || !mt::hasAnyMqttDownlinkEnabled(settings_) ||
        !bytes || !size || size > 512 || !chat::mqtt311::validTopic(topic, false)) return false;
    std::memset(&incoming_, 0, sizeof(incoming_));
    channel_.fill(0);
    received_gateway_.fill(0);
    auto stream = pb_istream_from_buffer(bytes, size);
    uint8_t fields = 0;
    while (stream.bytes_left)
    {
        pb_wire_type_t wire;
        uint32_t tag = 0;
        bool eof = false;
        if (!pb_decode_tag(&stream, &wire, &tag, &eof) || eof) return false;
        if (tag >= 1 && tag <= 3)
        {
            const uint8_t bit = uint8_t(1U << (tag - 1));
            if ((fields & bit) || wire != PB_WT_STRING) return false;
            fields |= bit;
            if (tag == meshtastic_ServiceEnvelope_packet_tag)
            {
                pb_istream_t substream;
                if (!pb_make_string_substream(&stream, &substream) || !pb_decode(&substream, meshtastic_MeshPacket_fields, &incoming_) ||
                    !pb_close_string_substream(&stream, &substream)) return false;
            }
            else if (!readString(stream, tag == meshtastic_ServiceEnvelope_channel_id_tag ? channel_.data() : received_gateway_.data(), channel_.size())) return false;
        }
        else if (!pb_skip_field(&stream, wire)) return false;
    }
    if ((fields & 3) != 3 || !variant(incoming_) || !incoming_.from || incoming_.from == 0xFFFFFFFFU) return false;
    const auto channel = mt::resolveMqttProxyDownlinkChannel(settings_, channel_.data());
    if (!channel.known || (channel.pki && incoming_.which_payload_variant != meshtastic_MeshPacket_encrypted_tag) || (incoming_.pki_encrypted && !channel.pki)) return false;
    if (!chat::runtime::resolveMeshtasticMqttDownlinkPolicy(received_gateway_.data(), self_, incoming_.from, incoming_.to, false).accept_locally) return false;
    const auto port = incoming_.which_payload_variant == meshtastic_MeshPacket_decoded_tag ? incoming_.decoded.portnum : meshtastic_PortNum_UNKNOWN_APP;
    if (mt::validateMqttDecodedDownlinkPayload(settings_, incoming_.which_payload_variant == meshtastic_MeshPacket_decoded_tag, port) != mt::MqttProxyRejectReason::None) return false;
    const std::string prefix = settings_.root + "/2/e/" + channel_.data() + "/";
    if (topic.size() <= prefix.size() || topic.substr(0, prefix.size()) != prefix) return false;
    const auto gateway = topic.substr(prefix.size());
    if (gateway.find('/') != gateway.npos || ((fields & 4) && gateway != received_gateway_.data())) return false;
    if (chat::runtime::mqttGatewayIdMatchesNode(std::string(gateway).c_str(), self_)) return false;
    output.packet = &incoming_;
    output.channel = channel.channel;
    output.pki = channel.pki;
    output.retained = retained;
    return true;
}
} // namespace trailmate::linux_app
