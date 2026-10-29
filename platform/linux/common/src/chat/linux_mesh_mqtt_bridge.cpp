#include "chat/linux_mesh_mqtt_bridge.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"
#include "chat/infra/mqtt/mqtt311_codec.h"
#include "sys/clock.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <pb_decode.h>
#include <thread>

namespace trailmate::linux_app
{
struct LinuxMeshMqttBridge::Impl
{
    Config config;
    Clock clock;
    bool requested = false, valid = false;
    std::unique_ptr<LinuxMqttClient> client;
    std::unique_ptr<LinuxMeshtasticMqttCodec> mt;
    LinuxMeshtasticMqttCodec::Envelope output;
    LinuxMqttClient::Message input;
    meshtastic_MeshPacket transmit_packet{};
    std::string publish_topic, vmp_topic;
    uint64_t next_token = 0;
    uint64_t generation = 0, observed_client_generation = 0;
    std::function<bool()> cancelled;
    char error[96]{};
    explicit Impl(Clock value) : clock(value ? value : ::sys::millis_now) {}
    void synchronizeGeneration()
    {
        const auto current = client ? client->snapshot().generation : 0;
        if (current && current != observed_client_generation)
        {
            observed_client_generation = current;
            if (++generation == 0) ++generation;
        }
    }
    bool configure(const Config& value, chat::NodeId self)
    {
        // A complete application configuration is a new receive cohort, even
        // when its endpoint is unchanged (e.g. channel/key/identity mutation).
        client.reset();
        mt.reset();
        valid = false;
        error[0] = 0;
        if (++generation == 0) ++generation;
        observed_client_generation = 0;
        config = value;
        requested = value.client.enabled;
        if (!requested) return true;
        const auto invalid = [&]
        { std::snprintf(error, sizeof(error), "Invalid mesh MQTT configuration"); return false; };
        if (!self || self == 0xFFFFFFFFU || (value.protocol != chat::MeshProtocol::Meshtastic && value.protocol != chat::MeshProtocol::MeshCore) ||
            value.root.empty() || value.root.size() > 64 || !chat::mqtt311::validTopic(value.root, false)) return invalid();
        auto options = value.client;
        if (options.client_id.empty())
        {
            char identifier[32];
            std::snprintf(identifier, sizeof(identifier), value.protocol == chat::MeshProtocol::MeshCore ? "tm-mc-%08lx" : "tm-%08lx", static_cast<unsigned long>(self));
            options.client_id = identifier;
        }
        options.subscription = value.downlink ? value.root + (value.protocol == chat::MeshProtocol::MeshCore ? "/raw/#" : "/2/e/#") : "";
        if (value.protocol == chat::MeshProtocol::MeshCore && !chat::mqtt311::validTopic(value.root + "/raw/" + options.client_id, false)) return invalid();
        auto prepared_client = std::make_unique<LinuxMqttClient>();
        if (!prepared_client->configure(options)) return invalid();
        auto prepared_mt = std::make_unique<LinuxMeshtasticMqttCodec>();
        if (value.protocol == chat::MeshProtocol::Meshtastic)
        {
            auto settings = value.meshtastic;
            settings.root = value.root;
            settings.enabled = settings.proxy_to_client_enabled = true;
            settings.primary_uplink_enabled = settings.primary_uplink_enabled && value.uplink;
            settings.secondary_uplink_enabled = settings.secondary_uplink_enabled && value.uplink;
            settings.primary_downlink_enabled = settings.primary_downlink_enabled && value.downlink;
            settings.secondary_downlink_enabled = settings.secondary_downlink_enabled && value.downlink;
            if (!prepared_mt->configure(settings, self)) return invalid();
        }
        config.client = options;
        publish_topic = value.root + "/raw/" + options.client_id;
        vmp_topic = value.root + "/2/e/vmp";
        client = std::move(prepared_client);
        mt = std::move(prepared_mt);
        valid = true;
        return true;
    }
    bool ready() const { return valid && client && client->snapshot().state == LinuxMqttClient::State::Online; }
    bool write(const char* topic, const uint8_t* bytes, size_t size)
    {
        if (!ready() || !config.uplink || (cancelled && cancelled())) return false;
        if (++next_token == 0) ++next_token;
        const auto token = next_token, generation = client->snapshot().generation;
        if (!client->publish(topic, bytes, size, token)) return false;
        const auto started = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - started < std::chrono::milliseconds(config.client.pending_ms))
        {
            if (cancelled && cancelled())
            {
                client->clear();
                return false;
            }
            client->tick(clock());
            synchronizeGeneration();
            LinuxMqttClient::Outcome outcome;
            while (client->outcome(outcome))
                if (outcome.token == token) return outcome.written && outcome.generation == generation;
            if (!ready() && client->snapshot().generation != generation) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        // This also bounds completion when a test/injected logical clock stops.
        client->clear();
        (void)client->configure(config.client);
        LinuxMqttClient::Outcome discarded;
        while (client->outcome(discarded))
        {
        }
        return false;
    }
};
LinuxMeshMqttBridge::LinuxMeshMqttBridge(Clock clock) : impl_(std::make_unique<Impl>(clock)) {}
LinuxMeshMqttBridge::~LinuxMeshMqttBridge() = default;
bool LinuxMeshMqttBridge::configure(const Config& config, chat::NodeId self) { return impl_->configure(config, self); }
void LinuxMeshMqttBridge::tick()
{
    if (impl_->client) impl_->client->tick(impl_->clock());
    impl_->synchronizeGeneration();
}
void LinuxMeshMqttBridge::clear() { (void)impl_->configure({}, 0); }
void LinuxMeshMqttBridge::setCancellation(std::function<bool()> callback) { impl_->cancelled = std::move(callback); }
bool LinuxMeshMqttBridge::active() const { return impl_->requested; }
bool LinuxMeshMqttBridge::ready() const { return impl_->ready(); }
LinuxMqttClient::Snapshot LinuxMeshMqttBridge::snapshot() const
{
    auto result = impl_->client ? impl_->client->snapshot() : LinuxMqttClient::Snapshot{};
    result.generation = impl_->generation;
    if (impl_->requested && !impl_->valid)
    {
        result.state = LinuxMqttClient::State::Waiting;
        std::snprintf(result.error, sizeof(result.error), "%s", impl_->error);
    }
    return result;
}
bool LinuxMeshMqttBridge::publishMeshtastic(const meshtastic_MeshPacket& packet, chat::ChannelId channel, bool from_mqtt)
{
    auto& state = *impl_;
    return state.config.protocol == chat::MeshProtocol::Meshtastic && state.mt && state.ready() &&
           state.mt->encode(packet, channel, from_mqtt, state.output) && state.write(state.output.topic.data(), state.output.bytes.data(), state.output.size);
}
bool LinuxMeshMqttBridge::publishMeshCore(const uint8_t* bytes, size_t size, bool from_mqtt)
{
    auto& state = *impl_;
    chat::meshcore::ParsedPacket parsed;
    return state.config.protocol == chat::MeshProtocol::MeshCore && !from_mqtt && bytes && size && size <= 255 &&
           chat::meshcore::parsePacket(bytes, size, &parsed) && chat::meshcore::isSupportedPayloadVersion(parsed.payload_ver) &&
           state.write(state.publish_topic.c_str(), bytes, size);
}
bool LinuxMeshMqttBridge::publishMeshtasticPayload(const chat::meshtastic::PacketHeaderWire& header, chat::ChannelId channel,
                                                   const uint8_t* bytes, size_t size, bool pki_ciphertext)
{
    if (!bytes || !size || !ready() || impl_->config.protocol != chat::MeshProtocol::Meshtastic) return false;
    auto& packet = impl_->transmit_packet;
    std::memset(&packet, 0, sizeof(packet));
    packet.from = header.from;
    packet.to = header.to;
    packet.id = header.id;
    packet.channel = pki_ciphertext ? 0 : static_cast<uint8_t>(channel);
    packet.hop_limit = header.flags & chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
    packet.hop_start = header.flags >> chat::meshtastic::PACKET_FLAGS_HOP_START_SHIFT;
    packet.want_ack = header.flags & chat::meshtastic::PACKET_FLAGS_WANT_ACK_MASK;
    packet.via_mqtt = header.flags & chat::meshtastic::PACKET_FLAGS_VIA_MQTT_MASK;
    packet.next_hop = header.next_hop;
    packet.relay_node = header.relay_node;
    packet.pki_encrypted = pki_ciphertext;
    if (pki_ciphertext)
    {
        if (size > sizeof(packet.encrypted.bytes)) return false;
        packet.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
        packet.encrypted.size = static_cast<pb_size_t>(size);
        std::memcpy(packet.encrypted.bytes, bytes, size);
    }
    else
    {
        packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        auto stream = pb_istream_from_buffer(bytes, size);
        if (!pb_decode(&stream, meshtastic_Data_fields, &packet.decoded)) return false;
    }
    return publishMeshtastic(packet, channel, packet.via_mqtt);
}
bool LinuxMeshMqttBridge::publishVmp(const uint8_t* bytes, size_t size)
{
    // Validation/authentication belongs to the VMP owner, not an MT/MC decoder.
    return impl_->config.protocol == chat::MeshProtocol::Meshtastic && bytes && size && size <= 512 &&
           impl_->write(impl_->vmp_topic.c_str(), bytes, size);
}
bool LinuxMeshMqttBridge::receive(Incoming& incoming)
{
    auto& state = *impl_;
    incoming = {};
    if (!state.ready() || !state.config.downlink) return false;
    for (unsigned budget = 0; budget < 16 && state.client->receive(state.input); ++budget)
    {
        const auto& input = state.input;
        if (input.generation != state.client->snapshot().generation) continue;
        incoming.generation = state.generation;
        const std::string_view topic(input.topic.data());
        if (state.config.protocol == chat::MeshProtocol::MeshCore)
        {
            chat::meshcore::ParsedPacket parsed;
            const auto prefix = state.config.root + "/raw/";
            if (input.retained || topic == state.publish_topic || topic.size() <= prefix.size() || topic.substr(0, prefix.size()) != prefix ||
                input.size > incoming.meshcore.bytes.size() || !chat::meshcore::parsePacket(input.bytes.data(), input.size, &parsed) ||
                !chat::meshcore::isSupportedPayloadVersion(parsed.payload_ver)) continue;
            incoming.kind = Incoming::Kind::MeshCore;
            incoming.meshcore.size = input.size;
            std::memcpy(incoming.meshcore.bytes.data(), input.bytes.data(), input.size);
            return true;
        }
        if (topic == state.vmp_topic)
        {
            if (input.retained || !input.size) continue;
            incoming.kind = Incoming::Kind::Vmp;
            incoming.vmp_size = input.size;
            std::memcpy(incoming.vmp.data(), input.bytes.data(), input.size);
            return true;
        }
        if (state.mt->decode(topic, input.bytes.data(), input.size, input.retained, incoming.meshtastic))
        {
            incoming.kind = Incoming::Kind::Meshtastic;
            return true;
        }
    }
    incoming = {};
    return false;
}
} // namespace trailmate::linux_app
