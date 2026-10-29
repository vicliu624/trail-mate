#include "chat/linux_meshcore_discovery.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace trailmate::linux_app::meshcore_wire
{
namespace
{
namespace mc = chat::meshcore;
mc::PayloadProfile profile(const chat::MeshConfig& config)
{
    return config.meshcore_send_profile == chat::MeshCorePayloadSendProfile::V1Only
               ? mc::PayloadProfile::V1
               : mc::PayloadProfile::V2;
}
bool controlFrame(const chat::MeshConfig& config, const uint8_t* data, std::size_t size, Frame& frame)
{
    return mc::buildFrameNoTransport(profile(config), 2, mc::kMeshCorePayloadTypeControl,
                                     nullptr, 0, data, size, frame.bytes.data(), frame.bytes.size(), &frame.size);
}
} // namespace
bool Discovery::makeRequest(const chat::MeshConfig& config, uint32_t tag, Frame& frame) const
{
    frame = {};
    if (!tag) return false;
    mc::MeshCoreDiscoverRequestBuildInfo request;
    request.tag = tag;
    request.type_filter = mc::kMeshCoreDiscoverTypeFilterAll;
    uint8_t data[10]{};
    std::size_t size = 0;
    return mc::buildDiscoverRequestControlPayload(request, data, sizeof(data), &size) && controlFrame(config, data, size, frame);
}
void Discovery::arm(uint32_t tag, uint32_t now)
{
    tag_ = tag;
    started_ = now;
    armed_ = tag != 0;
    replies_ = {};
}
bool Discovery::listening(uint32_t now) const
{
    return armed_ && uint32_t(now - started_) < window_ms;
}
void Discovery::reset()
{
    armed_ = false;
    tag_ = 0;
    pending_ = {};
    requests_ = {};
    replies_ = {};
}
bool Discovery::remember(std::array<Seen, 32>& items, uint32_t value, uint32_t now)
{
    for (const auto& item : items)
        if (item.valid && item.value == value && uint32_t(now - item.started) < window_ms) return false;
    for (auto& item : items)
        if (!item.valid || uint32_t(now - item.started) >= window_ms)
        {
            item = {value, now, true};
            return true;
        }
    return false; // Refuse excess distinct requests/responses during this window.
}
bool Discovery::receive(const chat::MeshConfig& config, const std::array<uint8_t, 32>& own_key,
                        const platform::linux_runtime::RadioPacket& packet, uint32_t now,
                        uint32_t epoch, uint32_t random, DiscoveredPeer* peer)
{
    if (peer) *peer = {};
    mc::ParsedPacket parsed;
    if (packet.size > packet.data.size() || !mc::parsePacket(packet.data.data(), packet.size, &parsed) ||
        !mc::isSupportedPayloadVersion(parsed.payload_ver) || parsed.path_len != 0 ||
        parsed.payload_type != mc::kMeshCorePayloadTypeControl) return false;
    mc::DecodedDiscoverRequest request;
    if ((parsed.payload_len == 6 || parsed.payload_len == 10) && mc::decodeDiscoverRequest(parsed.payload, parsed.payload_len, &request))
    {
        const auto local_type = config.meshcore_client_repeat ? mc::kMeshCoreAdvertTypeRepeater : mc::kMeshCoreAdvertTypeChat;
        if (!config.tx_enabled || !mc::discoverFilterMatchesType(request.type_filter, local_type) ||
            (request.since >= 1577836800U && epoch >= 1577836800U && epoch < request.since) ||
            (listening(now) && request.tag == tag_)) return false;
        Pending* available = nullptr;
        for (auto& item : pending_)
            if (!item.valid || uint32_t(now - item.started) >= window_ms)
            {
                available = &item;
                break;
            }
        if (!available || !remember(requests_, request.tag, now)) return false;
        const auto snr = std::isfinite(packet.snr_db) ? std::clamp(std::lround(packet.snr_db * 4.0f), -128L, 127L) : 0;
        uint8_t data[38]{};
        std::size_t size = 0;
        Frame frame;
        if (!mc::buildDiscoverResponseControlPayload(local_type, int8_t(snr), request.tag,
                                                     own_key.data(), request.prefix_only ? 8 : own_key.size(), data, sizeof(data), &size) ||
            !controlFrame(config, data, size, frame)) return false;
        const float airtime = mc::estimateLoRaAirtimeMs(frame.size, config.meshcore_bw_khz, config.meshcore_sf, config.meshcore_cr);
        const float delay = (std::isfinite(airtime) && airtime > 0 ? airtime : 50.0f) * 2 * (1 + random % 5);
        *available = {frame, now, uint32_t(std::clamp(delay, 100.0f, 2000.0f)), true};
        return false;
    }
    mc::DecodedDiscoverResponse response;
    if (!listening(now) || parsed.payload_len != 38 || !mc::decodeDiscoverResponse(parsed.payload, parsed.payload_len, &response) ||
        response.tag != tag_ || response.pubkey_len != 32 ||
        !std::memcmp(response.pubkey, own_key.data(), own_key.size()) ||
        !mc::discoverFilterMatchesType(mc::kMeshCoreDiscoverTypeFilterAll, response.node_type)) return false;
    const auto node = mc::deriveNodeIdFromPubkey(response.pubkey, response.pubkey_len);
    if (!node || node == 0xFFFFFFFFU || !remember(replies_, node, now)) return false;
    if (peer) *peer = {node, mc::mapAdvertTypeToRole(response.node_type), response.snr_qdb};
    return true;
}
bool Discovery::takeResponse(uint32_t now, Frame& frame)
{
    frame = {};
    if (armed_ && uint32_t(now - started_) >= window_ms) armed_ = false;
    for (auto& item : pending_)
    {
        if (!item.valid) continue;
        const auto elapsed = uint32_t(now - item.started);
        if (elapsed >= window_ms)
        {
            item.valid = false;
            continue;
        }
        if (listening(now) || elapsed < item.delay) continue;
        frame = item.frame;
        item.valid = false;
        return true;
    }
    return false;
}
} // namespace trailmate::linux_app::meshcore_wire
