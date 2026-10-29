#include "chat/linux_meshcore_repeater.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace trailmate::linux_app::meshcore_wire
{
namespace
{
namespace mc = chat::meshcore;
constexpr uint8_t kAck = 3, kAdvert = 4, kMultipart = 10;
constexpr uint32_t kSeenLifetime = 60000, kAckSpacing = 300;
bool forwardingProfile(const chat::MeshConfig& config, mc::PayloadProfile profile)
{
    return config.meshcore_forward_profile == chat::MeshCoreForwardProfile::Any || mc::payloadHashBytes(profile) > 1;
}
bool rewrite(const platform::linux_runtime::RadioPacket& input, const mc::ParsedPacket& parsed,
             mc::PayloadProfile path_profile, const uint8_t* path, size_t path_size, Frame& output)
{
    const size_t prefix = parsed.path_len_index + 1;
    output.size = prefix + path_size + parsed.payload_len;
    if (output.size > output.bytes.size() || !mc::encodePathDescriptor(path_profile, path_size, &output.bytes[parsed.path_len_index])) return false;
    std::memcpy(output.bytes.data(), input.data.data(), prefix - 1);
    if (path_size) std::memcpy(output.bytes.data() + prefix, path, path_size);
    if (parsed.payload_len) std::memcpy(output.bytes.data() + prefix + path_size, parsed.payload, parsed.payload_len);
    return true;
}
} // namespace

bool Repeater::seen(uint32_t signature, uint32_t now) const
{
    for (const auto& entry : seen_)
        if (entry.valid && entry.signature == signature && uint32_t(now - entry.started) < kSeenLifetime) return true;
    for (const auto& entry : pending_)
        if (entry.valid && entry.signature == signature && uint32_t(now - entry.started) < entry.lifetime) return true;
    return false;
}
void Repeater::remember(uint32_t signature, uint32_t now)
{
    seen_[cursor_] = {signature, now, true};
    cursor_ = (cursor_ + 1) % seen_.size();
}
void Repeater::sent(const uint8_t* bytes, size_t size, uint32_t now)
{
    mc::ParsedPacket parsed;
    if (bytes && size <= 255 && mc::parsePacket(bytes, size, &parsed) && mc::isSupportedPayloadVersion(parsed.payload_ver))
        remember(mc::packetSignature(parsed.payload_type, parsed.path_len, parsed.payload, parsed.payload_len), now);
}
bool Repeater::enqueue(const Frame& frame, uint32_t signature, uint32_t now, uint32_t delay, uint32_t lifetime)
{
    for (auto& entry : pending_)
        if (!entry.valid || uint32_t(now - entry.started) >= entry.lifetime)
        {
            entry = {frame, signature, now, delay, lifetime, true};
            return true;
        }
    return false; // Drop new work; never evict an admitted forwarding frame.
}
bool Repeater::take(uint32_t now, Frame& frame)
{
    frame = {};
    Pending* selected = nullptr;
    for (auto& entry : pending_)
    {
        if (!entry.valid) continue;
        const uint32_t age = now - entry.started;
        if (age >= entry.lifetime)
        {
            entry.valid = false;
            continue;
        }
        if (age >= entry.delay && (!selected || uint32_t(now - selected->started) < age)) selected = &entry;
    }
    if (!selected) return false;
    frame = selected->frame;
    selected->valid = false;
    return true;
}
void Repeater::reset()
{
    pending_ = {};
    seen_ = {};
    cursor_ = 0;
}
size_t Repeater::queued() const
{
    size_t count = 0;
    for (const auto& entry : pending_)
        if (entry.valid) ++count;
    return count;
}

Repeater::Disposition Repeater::receive(const chat::MeshConfig& config, const std::array<uint8_t, 32>& own_key,
                                        const platform::linux_runtime::RadioPacket& packet, uint32_t now, uint32_t random)
{
    mc::ParsedPacket parsed;
    if (!packet.size || packet.size > packet.data.size() || !mc::parsePacket(packet.data.data(), packet.size, &parsed) ||
        !mc::isSupportedPayloadVersion(parsed.payload_ver)) return Disposition::Transit;
    const auto profile = mc::payloadProfileFromVersion(parsed.payload_ver);
    const size_t hash_size = mc::payloadHashBytes(profile);
    const bool direct = parsed.route_type == 2 || parsed.route_type == 3;
    const auto disposition = direct && parsed.path_len ? Disposition::Transit : Disposition::Local;
    const auto signature = mc::packetSignature(parsed.payload_type, parsed.path_len, parsed.payload, parsed.payload_len);
    const bool enabled = config.meshcore_client_repeat && config.tx_enabled;
    Frame frame;
    if (direct && parsed.payload_type == mc::kMeshCorePayloadTypeTrace)
    {
        mc::DecodedTracePayload trace;
        if (!mc::decodeTracePayload(parsed.payload, parsed.payload_len, parsed.path_len, &trace)) return Disposition::Transit;
        if (trace.terminal) return Disposition::Local;
        if (!enabled || trace.next_hash != own_key[0] || parsed.path_hash_bytes != 1 ||
            parsed.path_len >= mc::kMeshCoreMaxPathBytes || seen(signature, now)) return Disposition::Transit;
        std::array<uint8_t, 64> path{};
        std::memcpy(path.data(), parsed.path, parsed.path_len);
        const long snr = std::isfinite(packet.snr_db) ? std::clamp(std::lround(packet.snr_db * 4), -128L, 127L) : 0;
        path[parsed.path_len] = static_cast<uint8_t>(static_cast<int8_t>(snr));
        if (rewrite(packet, parsed, mc::PayloadProfile::V1, path.data(), parsed.path_len + 1, frame) &&
            enqueue(frame, signature, now, 0, 5000)) remember(signature, now);
        return Disposition::Transit;
    }
    if (!mc::pathIsWellFormed(profile, parsed.path_len)) return Disposition::Transit;
    // Upstream discovery controls are local zero-hop exchanges, not relays.
    if (parsed.payload_type == mc::kMeshCorePayloadTypeControl && parsed.payload_len && (parsed.payload[0] & 0x80))
        return parsed.path_len ? Disposition::Transit : Disposition::Local;
    if (!enabled || !forwardingProfile(config, profile) || seen(signature, now)) return disposition;
    if (direct)
    {
        if (!parsed.path_len || std::memcmp(parsed.path, own_key.data(), hash_size)) return disposition;
        const size_t remaining_path = parsed.path_len - hash_size;
        const uint8_t* path = parsed.path + hash_size;
        if (parsed.payload_type == kAck || parsed.payload_type == kMultipart)
        {
            const bool multipart = parsed.payload_type == kMultipart;
            if (!remaining_path || parsed.payload_len < (multipart ? 5U : 4U) || (multipart && (parsed.payload[0] & 15) != kAck)) return disposition;
            const uint8_t remaining = multipart ? parsed.payload[0] >> 4 : 0;
            const uint8_t* token = parsed.payload + (multipart ? 1 : 0);
            const uint32_t delay = multipart ? uint32_t(remaining + 1) * kAckSpacing : 0;
            size_t free = 0;
            for (const auto& entry : pending_)
                if (!entry.valid || uint32_t(now - entry.started) >= entry.lifetime) ++free;
            if (free < (config.meshcore_multi_acks ? 2U : 1U)) return disposition;
            if (config.meshcore_multi_acks)
            {
                uint8_t payload[5]{static_cast<uint8_t>((remaining << 4) | kAck)};
                std::memcpy(payload + 1, token, 4);
                if (!mc::buildFrameNoTransport(profile, 2, kMultipart, path, remaining_path, payload, 5,
                                               frame.bytes.data(), frame.bytes.size(), &frame.size)) return disposition;
                enqueue(frame, signature, now, delay, delay + 5300);
            }
            if (mc::buildFrameNoTransport(profile, 2, kAck, path, remaining_path, token, 4,
                                          frame.bytes.data(), frame.bytes.size(), &frame.size))
            {
                const uint32_t final_delay = delay + (config.meshcore_multi_acks ? kAckSpacing : 0);
                enqueue(frame, signature, now, final_delay, final_delay + 5000);
                remember(signature, now);
            }
            return disposition;
        }
        if (rewrite(packet, parsed, profile, path, remaining_path, frame) && enqueue(frame, signature, now, 0, 5000)) remember(signature, now);
        return disposition;
    }
    if (parsed.payload_type > 8 || parsed.path_hop_count >= config.meshcore_flood_max ||
        parsed.path_len + hash_size > mc::kMeshCoreMaxPathBytes || packet.size + hash_size > frame.bytes.size()) return disposition;
    const bool anonymous = parsed.payload_type == 7 && mc::isAnonReqCipherShape(profile, parsed.payload_len) && !mc::isPeerCipherShape(profile, parsed.payload_len);
    const bool peer = mc::isPeerPayloadType(parsed.payload_type) && mc::isPeerCipherShape(profile, parsed.payload_len) && !anonymous;
    if ((anonymous || peer) && parsed.payload_len >= hash_size && !std::memcmp(parsed.payload, own_key.data(), hash_size)) return disposition;
    if (parsed.payload_type == kAdvert && parsed.payload_len >= own_key.size() && !std::memcmp(parsed.payload, own_key.data(), own_key.size())) return disposition;
    for (size_t offset = 0; offset < parsed.path_len; offset += hash_size)
        if (!std::memcmp(parsed.path + offset, own_key.data(), hash_size)) return disposition;
    std::array<uint8_t, 64> path{};
    std::memcpy(path.data(), parsed.path, parsed.path_len);
    std::memcpy(path.data() + parsed.path_len, own_key.data(), hash_size);
    if (!rewrite(packet, parsed, profile, path.data(), parsed.path_len + hash_size, frame)) return disposition;
    const double airtime = mc::estimateLoRaAirtimeMs(frame.size, config.meshcore_bw_khz, config.meshcore_sf, config.meshcore_cr);
    if (!std::isfinite(airtime) || airtime <= 0 || airtime > 1000000 || !std::isfinite(config.meshcore_airtime_factor) ||
        config.meshcore_airtime_factor < 0 || !std::isfinite(config.meshcore_rx_delay_base)) return disposition;
    const uint32_t air = static_cast<uint32_t>(std::llround(airtime));
    const float score = mc::scoreFromSnr(std::isfinite(packet.snr_db) ? packet.snr_db : -30, config.meshcore_sf, packet.size);
    const uint32_t rx_delay = mc::computeRxDelayMs(config.meshcore_rx_delay_base, score, air);
    const double step = std::round(airtime * config.meshcore_airtime_factor);
    const double delay = rx_delay + (random % 7) * step;
    const double lifetime = delay + std::max(5000.0, airtime * 3);
    if (!std::isfinite(lifetime) || lifetime >= std::numeric_limits<int32_t>::max()) return disposition;
    if (enqueue(frame, signature, now, static_cast<uint32_t>(delay), static_cast<uint32_t>(lifetime))) remember(signature, now);
    return disposition;
}
} // namespace trailmate::linux_app::meshcore_wire
