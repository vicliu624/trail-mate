#include "chat/linux_meshcore_group_wire.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include <algorithm>
#include <cstring>

namespace trailmate::linux_app::meshcore_wire
{
namespace
{
namespace mc = chat::meshcore;
constexpr std::size_t max_payload = 184;
const uint8_t* channelKey(const chat::MeshConfig& config, chat::ChannelId channel)
{
    const auto slot = chat::meshCoreChannelSlotFromId(channel);
    if (static_cast<uint8_t>(channel) >= chat::kMeshCoreChannelMaxCount) return nullptr;
    const auto& selected = config.meshCoreChannel(slot);
    if (!selected.enabled) return nullptr;
    if (!mc::isZeroKey(selected.key, chat::kMeshCoreChannelKeyLen)) return selected.key;
    return slot == 0 && mc::shouldUsePublicChannelFallback(config) ? mc::publicGroupPsk() : nullptr;
}
mc::PayloadProfile profileFor(const chat::MeshConfig& config)
{
    return config.meshcore_send_profile == chat::MeshCorePayloadSendProfile::V1Only
               ? mc::PayloadProfile::V1
               : mc::PayloadProfile::V2;
}
void put32(uint8_t* out, uint32_t value)
{
    for (unsigned index = 0; index < 4; ++index) out[index] = uint8_t(value >> (index * 8));
}
uint32_t get32(const uint8_t* in)
{
    uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index) value |= uint32_t(in[index]) << (index * 8);
    return value;
}
std::size_t plainBudget(mc::PayloadProfile profile)
{
    return ((max_payload - mc::payloadHashBytes(profile) - mc::payloadMacBytes(profile)) / 16) * 16;
}
bool encode(const chat::MeshConfig& config, chat::ChannelId channel, uint8_t type,
            const uint8_t* plain, std::size_t size, Frame& frame)
{
    frame = {};
    const auto* key = channelKey(config, channel);
    const auto profile = profileFor(config);
    if (!key || !plain || !size || size > plainBudget(profile)) return false;
    uint8_t key32[32]{};
    mc::toHmacKey32(key, key32);
    std::array<uint8_t, max_payload> payload{};
    const auto hash_size = mc::payloadHashBytes(profile);
    if (!mc::computeChannelHashBytes(key, payload.data(), hash_size)) return false;
    const auto encrypted = mc::encryptThenMac(key, key32, payload.data() + hash_size,
                                              payload.size() - hash_size, plain, size,
                                              mc::payloadMacBytes(profile));
    return encrypted && mc::buildFrameNoTransport(profile, 1, type, nullptr, 0,
                                                  payload.data(), hash_size + encrypted,
                                                  frame.bytes.data(), frame.bytes.size(), &frame.size);
}
} // namespace
bool encodeText(const chat::MeshConfig& config, chat::ChannelId channel,
                const std::string& text, uint32_t timestamp, Frame& frame)
{
    frame = {};
    if (text.empty() || text.find('\0') != std::string::npos) return false;
    std::array<uint8_t, max_payload> plain{};
    // Match the production adapter: ts LE32, plain-text type, decorated text.
    // Reject overlong input rather than silently truncating UTF-8 or messages.
    if (text.size() + 5 > plainBudget(profileFor(config))) return false;
    put32(plain.data(), timestamp);
    std::memcpy(plain.data() + 5, text.data(), text.size());
    return encode(config, channel, 5, plain.data(), text.size() + 5, frame);
}
bool encodeData(const chat::MeshConfig& config, chat::ChannelId channel,
                chat::NodeId sender, uint32_t port, const uint8_t* payload,
                std::size_t size, Frame& frame)
{
    frame = {};
    // The production GD envelope has no length field and trims zero padding.
    // Refuse an ambiguous tail instead of reporting a lossy send as success.
    if (!sender || sender == 0xFFFFFFFFU || !port || !payload || !size || (payload[size - 1] == 0 && !mc::isFixedNodeInfoPayload(port, payload, size)) ||
        size > plainBudget(profileFor(config)) - 10) return false;
    std::array<uint8_t, max_payload> plain{};
    plain[0] = 'G';
    plain[1] = 'D';
    put32(plain.data() + 2, sender);
    put32(plain.data() + 6, port);
    std::memcpy(plain.data() + 10, payload, size);
    return encode(config, channel, 6, plain.data(), size + 10, frame);
}
bool decode(const chat::MeshConfig& config, const uint8_t* bytes,
            std::size_t size, Incoming& incoming)
{
    incoming = {};
    mc::ParsedPacket parsed;
    if (!bytes || size > 255 || !mc::parsePacket(bytes, size, &parsed) ||
        parsed.payload_len > max_payload ||
        (parsed.payload_type != 5 && parsed.payload_type != 6)) return false;
    const auto profile = mc::payloadProfileFromVersion(parsed.payload_ver);
    const auto hash_size = mc::payloadHashBytes(profile);
    if (parsed.payload_len <= hash_size + mc::payloadMacBytes(profile)) return false;
    for (uint8_t slot = 0; slot < chat::kMeshCoreChannelMaxCount; ++slot)
    {
        const auto channel = chat::meshCoreChannelIdFromSlot(slot);
        const auto* key = channelKey(config, channel);
        std::array<uint8_t, 2> expected{};
        if (!key || !mc::computeChannelHashBytes(key, expected.data(), hash_size) ||
            std::memcmp(expected.data(), parsed.payload, hash_size)) continue;
        uint8_t key32[32]{};
        mc::toHmacKey32(key, key32);
        std::array<uint8_t, max_payload> plain{};
        std::size_t plain_size = 0;
        if (!mc::macThenDecrypt(key, key32, parsed.payload + hash_size,
                                parsed.payload_len - hash_size, plain.data(), &plain_size,
                                mc::payloadMacBytes(profile))) continue;
        plain_size = parsed.payload_type == 6 ? mc::trimGroupPlaintextPadding(plain.data(), plain_size) : mc::trimTrailingZeros(plain.data(), plain_size);
        incoming.channel = channel;
        incoming.channel_hash = parsed.payload[0];
        incoming.hops = static_cast<uint8_t>(parsed.path_hop_count);
        incoming.signature = mc::packetSignature(parsed.payload_type, 0, parsed.payload, parsed.payload_len);
        if (parsed.payload_type == 5)
        {
            if (plain_size <= 5) return false;
            const auto type = plain[4] >> 2;
            const std::size_t offset = type == 2 ? 9 : 5;
            if ((type != 0 && type != 2) || plain_size <= offset) return false;
            incoming.text = true;
            incoming.timestamp = get32(plain.data());
            incoming.sender = type == 2 ? get32(plain.data() + 5) : 0;
            incoming.size = plain_size - offset;
            std::memcpy(incoming.payload.data(), plain.data() + offset, incoming.size);
        }
        else
        {
            mc::DecodedGroupAppPayload decoded;
            if (!mc::decodeGroupAppPayload(plain.data(), plain_size, &decoded)) return false;
            incoming.sender = decoded.sender;
            incoming.port = decoded.portnum;
            incoming.size = decoded.payload_len;
            std::memcpy(incoming.payload.data(), decoded.payload, decoded.payload_len);
        }
        return true;
    }
    incoming = {};
    return false;
}
} // namespace trailmate::linux_app::meshcore_wire
