#pragma once

#include "mesh/domain/mesh_result.h"

namespace mesh
{
namespace meshcore
{

class McPacketCodec
{
  public:
    ProtocolResult unsupported() const
    {
        return ProtocolResult::fail(ProtocolFailure::Unsupported);
    }
};

} // namespace meshcore
} // namespace mesh
