#pragma once
#include "chat/linux_meshcore_group_wire.h"
#include "mesh/domain/mesh_message.h"
#include "platform/linux/mesh/linux_meshcore_identity.h"
namespace trailmate::linux_app::meshcore_wire
{
bool encodeAdvert(const platform::linux_runtime::LinuxMeshCoreIdentity& identity,
                  const chat::MeshConfig& config, const std::string& name,
                  uint32_t timestamp, bool broadcast, Frame& frame);
bool decodeAdvert(const uint8_t* bytes, std::size_t size, mesh::MeshProtocolEvent& advert);
} // namespace trailmate::linux_app::meshcore_wire
