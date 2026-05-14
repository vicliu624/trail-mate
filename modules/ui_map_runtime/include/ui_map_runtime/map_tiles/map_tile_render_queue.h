#pragma once

#include "ui_map_runtime/map_tiles/map_tile_types.h"

#include <cstddef>
#include <cstdint>

namespace ui
{
namespace map_tiles
{

struct MapTileScreenRect
{
    int16_t x = 0;
    int16_t y = 0;
    int16_t width = 0;
    int16_t height = 0;
};

enum class MapTileRenderState : uint8_t
{
    Unknown,
    Missing,
    Loading,
    Ready,
    Error,
};

struct MapTileRenderRef
{
    MapTileRef tile;
    MapTileScreenRect rect;
    MapTileRenderState state = MapTileRenderState::Unknown;
};

class MapTileRenderQueue
{
  public:
    static constexpr std::size_t kMaxTiles = 64;

    void clear();
    bool push(const MapTileRenderRef& item);
    std::size_t size() const;
    const MapTileRenderRef* items() const;

  private:
    MapTileRenderRef items_[kMaxTiles]{};
    std::size_t size_ = 0;
};

} // namespace map_tiles
} // namespace ui
