#pragma once

#include "mesh/domain/mesh_result.h"

namespace mesh
{
namespace meshtastic
{

class MtDirectMessageFlow
{
  public:
    ProtocolResult unsupported() const
    {
        return ProtocolResult::fail(ProtocolFailure::Unsupported);
    }
};

} // namespace meshtastic
} // namespace mesh
