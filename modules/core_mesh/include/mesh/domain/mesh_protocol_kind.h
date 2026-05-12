#pragma once

#include <stdint.h>

namespace mesh
{

enum class MeshProtocolKind : uint8_t
{
    None = 0,
    Meshtastic,
    MeshCore,
};

} // namespace mesh
