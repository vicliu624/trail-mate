#pragma once

#include "ui/map_tiles/map_tile_types.h"

namespace ui
{
namespace map_tiles
{

class IMapTileDecoderCache
{
  public:
    virtual ~IMapTileDecoderCache() = default;

    virtual void clear() = 0;
    virtual bool hasDecoded(const MapTileRef& ref) const = 0;
};

} // namespace map_tiles
} // namespace ui
