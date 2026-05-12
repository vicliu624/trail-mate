#pragma once

#include "mesh/domain/mesh_result.h"
#include "mesh/domain/node_id.h"

namespace mesh
{

struct NodeInfoView
{
    NodeId node_id;
    uint32_t last_seen_ms = 0;
    bool direct_reachable = false;
};

class INodeStore
{
  public:
    virtual ~INodeStore() = default;

    virtual StoreResult get(NodeId node_id, NodeInfoView& out) = 0;
    virtual StoreResult put(const NodeInfoView& node) = 0;
};

} // namespace mesh
