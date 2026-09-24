#pragma once

#include "ui_presentation/map/map_marker_source.h"
#include <algorithm>
#include <limits>

namespace ui::map
{
// Construct only in the allocator supplied by the platform. No static table,
// full-record mirror, or per-entry allocation. The renderer borrows this data.
struct MapMarkerCache
{
    static constexpr std::size_t kCapacity = 32;
    struct Entry
    {
        MapMarker marker;
        uint64_t distance = 0;
    };
    Entry entries[kCapacity]{};
    alignas(std::max_align_t) unsigned char scratch[256]{};
    std::size_t count = 0;
    bool truncated = false;
    bool valid = false;
    MapMarkerStatus seen{};
    int64_t next_transition = std::numeric_limits<int64_t>::max();

    void reset()
    {
        count = 0;
        truncated = false;
        valid = false;
        next_transition = std::numeric_limits<int64_t>::max();
    }
    void offer(const MapMarker& marker, uint64_t distance)
    {
        std::size_t position = 0;
        while (position < count && (entries[position].distance < distance ||
                                    (entries[position].distance == distance && entries[position].marker.id < marker.id))) ++position;
        if (count == kCapacity)
        {
            truncated = true;
            if (position == kCapacity) return;
        }
        else ++count;
        for (std::size_t i = count - 1; i > position; --i) entries[i] = entries[i - 1];
        entries[position] = {marker, distance};
    }
    bool stale(const MapMarkerStatus& status) const
    {
        return !valid || seen.revision != status.revision || seen.ready != status.ready ||
               seen.clock_valid != status.clock_valid || status.now < seen.now || status.now >= next_transition;
    }
    void commit(const MapMarkerStatus& status)
    {
        seen = status;
        valid = true;
        for (std::size_t i = 0; i < count; ++i)
            if (entries[i].marker.active_until >= status.now &&
                entries[i].marker.active_until < std::numeric_limits<int64_t>::max())
                next_transition = std::min(next_transition, entries[i].marker.active_until + 1);
    }
};
static_assert(sizeof(MapMarkerCache) <= 2816, "Viewport marker cache must stay compact");
} // namespace ui::map
