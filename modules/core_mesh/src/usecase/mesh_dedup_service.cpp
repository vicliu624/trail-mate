#include "mesh/usecase/mesh_dedup_service.h"

namespace mesh
{

MeshDedupService::MeshDedupService(uint32_t ttl_ms)
    : ttl_ms_(ttl_ms)
{
}

bool MeshDedupService::accept(NodeId peer, uint32_t packet_id, uint32_t now_ms)
{
    if (packet_id == 0)
    {
        return true;
    }

    for (const auto& entry : entries_)
    {
        if (!entry.valid)
        {
            continue;
        }
        if (entry.peer == peer && entry.packet_id == packet_id &&
            static_cast<uint32_t>(now_ms - entry.seen_at_ms) <= ttl_ms_)
        {
            return false;
        }
    }

    Entry entry{};
    entry.peer = peer;
    entry.packet_id = packet_id;
    entry.seen_at_ms = now_ms;
    entry.valid = true;
    entries_[next_slot_] = entry;
    next_slot_ = (next_slot_ + 1) % kCapacity;
    return true;
}

void MeshDedupService::clear()
{
    for (auto& entry : entries_)
    {
        entry = Entry{};
    }
    next_slot_ = 0;
}

} // namespace mesh
