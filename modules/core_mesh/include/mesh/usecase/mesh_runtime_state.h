#pragma once

#include "mesh/domain/mesh_protocol_kind.h"
#include "mesh/domain/radio_packet.h"

namespace mesh
{

enum class MeshSessionState : uint8_t
{
    Stopped = 0,
    Starting,
    Ready,
    Degraded,
    Error,
};

struct MeshRuntimeConfig
{
    MeshProtocolKind protocol = MeshProtocolKind::None;
    RadioConfig radio{};
    bool tx_enabled = true;
};

} // namespace mesh
