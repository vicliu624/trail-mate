#pragma once

#include "ui_map_runtime/map_tiles/map_tile_types.h"

#include <cstddef>

namespace ui
{
namespace map_tiles
{

class MapTileResolver
{
  public:
    MapTileResolver();
    explicit MapTileResolver(const char* root_prefix);

    void setRootPrefix(const char* root_prefix);
    const char* rootPrefix() const;

    bool resolvePath(const MapTileRef& ref, char* out_path, std::size_t out_size) const;
    bool resolveDirectory(MapTileLayer layer, char* out_path, std::size_t out_size) const;

  private:
    static constexpr std::size_t kRootPrefixCapacity = 96;

    char root_prefix_[kRootPrefixCapacity]{};
};

} // namespace map_tiles
} // namespace ui
