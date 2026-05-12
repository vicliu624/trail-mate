#pragma once

#include "mesh/domain/node_id.h"

#include <stddef.h>
#include <stdint.h>

namespace mesh
{

class MeshDedupService
{
  public:
    explicit MeshDedupService(uint32_t ttl_ms = 60000);

    bool accept(NodeId peer, uint32_t packet_id, uint32_t now_ms);
    void clear();

  private:
    struct Entry
    {
        NodeId peer{};
        uint32_t packet_id = 0;
        uint32_t seen_at_ms = 0;
        bool valid = false;
    };

    static constexpr size_t kCapacity = 32;
    Entry entries_[kCapacity]{};
    size_t next_slot_ = 0;
    uint32_t ttl_ms_ = 60000;
};

} // namespace mesh
