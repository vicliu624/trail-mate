#pragma once

#include "ui_map_runtime/map_tiles/map_tile_types.h"

#include <cstddef>
#include <cstdint>

namespace ui
{
namespace map_tiles
{

class IMapTileSource
{
  public:
    virtual ~IMapTileSource() = default;

    virtual MapTileLookupResult lookup(const MapTileRef& ref) const = 0;

    virtual bool read(const MapTileRef& ref,
                      uint8_t* buffer,
                      std::size_t capacity,
                      std::size_t& out_size,
                      MapTileFormat& out_format) const = 0;
};

class IMapTileFileSystem
{
  public:
    virtual ~IMapTileFileSystem() = default;

    virtual bool exists(const char* path) const = 0;
    virtual bool isDirectory(const char* path) const = 0;
    virtual bool readFile(const char* path,
                          uint8_t* buffer,
                          std::size_t capacity,
                          std::size_t& out_size) const = 0;
};

} // namespace map_tiles
} // namespace ui
