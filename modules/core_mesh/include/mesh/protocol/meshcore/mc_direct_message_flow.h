#pragma once

#include "mesh/domain/mesh_result.h"

namespace mesh
{
namespace meshcore
{

class McDirectMessageFlow
{
  public:
    ProtocolResult unsupported() const
    {
        return ProtocolResult::fail(ProtocolFailure::Unsupported);
    }
};

} // namespace meshcore
} // namespace mesh
