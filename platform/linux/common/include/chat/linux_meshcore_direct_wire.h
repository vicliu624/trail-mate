#pragma once
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"
#include "chat/linux_meshcore_group_wire.h"
#include "platform/linux/mesh/linux_meshcore_identity.h"

namespace trailmate::linux_app::meshcore_wire
{
enum class DirectKind
{
    Text,
    Data,
    Ack
};
struct DirectIncoming
{
    DirectKind kind = DirectKind::Text;
    chat::meshcore::PayloadProfile profile = chat::meshcore::PayloadProfile::V1;
    uint32_t timestamp = 0, port = 0, signature = 0, ack = 0;
    bool want_ack = false;
    std::array<uint8_t, 184> payload{};
    std::size_t size = 0;
    std::array<uint8_t, 64> return_path{};
    std::size_t return_path_size = 0;
    // Only the encrypted PATH body authenticates an advertised outbound route.
    std::array<uint8_t, 64> advertised_path{};
    std::size_t advertised_path_size = 0;
    bool has_advertised_path = false;
};
using Identity = platform::linux_runtime::LinuxMeshCoreIdentity;
bool encodeDirectText(const Identity& self, const std::array<uint8_t, 32>& peer,
                      chat::meshcore::PayloadProfile profile, const std::string& text,
                      uint32_t timestamp, Frame& frame, uint32_t& ack);
bool encodeDirectData(const Identity& self, const std::array<uint8_t, 32>& peer,
                      chat::meshcore::PayloadProfile profile, uint32_t port,
                      const uint8_t* bytes, std::size_t size, bool want_ack,
                      Frame& frame, uint32_t& ack);
bool decodeDirect(const Identity& self, const std::array<uint8_t, 32>& peer,
                  const uint8_t* bytes, std::size_t size, DirectIncoming& message);
bool encodeDirectAck(const Identity& self, const std::array<uint8_t, 32>& peer,
                     const DirectIncoming& message, Frame& frame);
bool applyDirectPath(Frame& frame, chat::meshcore::PayloadProfile profile,
                     const uint8_t* path, std::size_t size);
} // namespace trailmate::linux_app::meshcore_wire
