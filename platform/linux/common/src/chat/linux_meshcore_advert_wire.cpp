#include "chat/linux_meshcore_advert_wire.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "mesh/protocol/meshcore/meshcore_protocol_strategy.h"
#include <cstring>
namespace trailmate::linux_app::meshcore_wire
{
bool encodeAdvert(const platform::linux_runtime::LinuxMeshCoreIdentity& identity,
                  const chat::MeshConfig& config, const std::string& name,
                  uint32_t timestamp, bool broadcast, Frame& frame)
{
    frame = {};
    if (!identity.ready()) return false;
    char sanitized[32]{};
    const auto name_size = chat::meshcore::copyPrintableAscii(name, sanitized, sizeof(sanitized));
    std::array<uint8_t, 184> payload{};
    std::memcpy(payload.data(), identity.publicKey().data(), 32);
    for (unsigned index = 0; index < 4; ++index) payload[32 + index] = uint8_t(timestamp >> (8 * index));
    payload[100] = (config.meshcore_client_repeat ? chat::meshcore::kMeshCoreAdvertTypeRepeater : chat::meshcore::kMeshCoreAdvertTypeChat) |
                   (name_size ? 0x80 : 0);
    std::memcpy(payload.data() + 101, sanitized, name_size);
    std::array<uint8_t, 68> signed_message{};
    std::memcpy(signed_message.data(), payload.data(), 36);
    std::memcpy(signed_message.data() + 36, payload.data() + 100, 1 + name_size);
    if (!identity.sign(signed_message.data(), 37 + name_size, payload.data() + 36)) return false;
    const auto profile = config.meshcore_send_profile == chat::MeshCorePayloadSendProfile::V1Only
                             ? chat::meshcore::PayloadProfile::V1
                             : chat::meshcore::PayloadProfile::V2;
    return chat::meshcore::buildFrameNoTransport(profile, broadcast ? 1 : 2, 4, nullptr, 0,
                                                 payload.data(), 101 + name_size,
                                                 frame.bytes.data(), frame.bytes.size(), &frame.size);
}
bool decodeAdvert(const uint8_t* bytes, std::size_t size, mesh::MeshProtocolEvent& advert)
{
    advert = {};
    chat::meshcore::ParsedPacket parsed;
    if (!bytes || size > 255 || !chat::meshcore::parsePacket(bytes, size, &parsed) ||
        parsed.payload_type != 4 || parsed.payload_len < 101 || parsed.payload_len > 184) return false;
    mesh::RadioRxPacket packet;
    std::memcpy(packet.bytes, bytes, size);
    packet.size = size;
    mesh::meshcore::MeshCoreProtocolStrategy strategy;
    if (!strategy.parseRadioPacket(packet, advert).ok ||
        advert.kind != mesh::MeshProtocolEventKind::PeerAdvertReceived)
    {
        advert = {};
        return false;
    }
    if (advert.advert.has_location &&
        (advert.advert.latitude_i6 < -90000000 || advert.advert.latitude_i6 > 90000000 ||
         advert.advert.longitude_i6 < -180000000 || advert.advert.longitude_i6 > 180000000))
    {
        advert = {};
        return false;
    }
    return true;
}
} // namespace trailmate::linux_app::meshcore_wire
