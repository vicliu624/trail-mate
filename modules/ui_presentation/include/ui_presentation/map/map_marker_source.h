#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::map
{
// Optional, read-only annotations. Storage and calendar semantics belong to the
// source; projection and bounded viewport selection belong to the consumer.
struct MapMarker
{
    int64_t active_until = 0;
    uint32_t id = 0;
    int32_t latitude_e7 = 0;
    int32_t longitude_e7 = 0;
    char title[40]{};
};
struct MapMarkerStatus
{
    int64_t now = 0;
    uint32_t revision = 0;
    bool ready = false;
    bool clock_valid = false;
};
using MapMarkerVisitor = void (*)(const MapMarker&, void*);
class IMapMarkerSource
{
  public:
    virtual ~IMapMarkerSource() = default;
    virtual MapMarkerStatus markerStatus() const = 0;
    // Scratch is aligned to max_align_t and owned by the viewport session.
    virtual bool visitMarkers(MapMarkerVisitor visitor, void* context,
                              void* scratch, std::size_t scratch_bytes) = 0;
};
struct MapMarkerBinding
{
    IMapMarkerSource* source = nullptr;
    void* (*allocate)(std::size_t) = nullptr;
    void (*release)(void*) = nullptr;
};
} // namespace ui::map
