#pragma once

#include "ui_map_runtime/map_tiles/map_tile_types.h"

#include <cstddef>
#include <cstdint>

namespace ui
{
namespace map_tiles
{

enum class MapTileReadStatus : uint8_t
{
    Ready,
    Missing,
    RetryLater,
    Error,
    Invalid,
};

// Optional file-stage evidence. Cached/generated sources leave available false;
// zero milliseconds on a measured stage is a valid sub-tick measurement.
struct MapTileReadTiming
{
    uint32_t lock_wait_ms = 0;
    uint32_t open_ms = 0;
    uint32_t read_ms = 0;
    bool available = false;
    uint32_t block_calls = 0;
    uint32_t block_sectors = 0;
    uint32_t block_max_sectors = 0;
    uint32_t block_us = 0;
    bool block_available = false;
};

struct MapTileReadResult
{
    MapTileReadStatus status = MapTileReadStatus::Error;
    std::size_t size = 0;
    int32_t error = -1;
    MapTileFormat format = MapTileFormat::Unknown;
    MapTileReadTiming timing{};
};

class IMapTileSource
{
  public:
    virtual ~IMapTileSource() = default;

    virtual MapTileLookupResult lookup(const MapTileRef& ref) const = 0;

    virtual MapTileReadResult read(const MapTileRef& ref,
                                   uint8_t* buffer,
                                   std::size_t capacity) const = 0;
};

class IMapTileFileSystem
{
  public:
    virtual ~IMapTileFileSystem() = default;

    virtual bool exists(const char* path) const = 0;
    virtual bool isDirectory(const char* path) const = 0;
    virtual MapTileReadResult readFile(const char* path,
                                       uint8_t* buffer,
                                       std::size_t capacity) const = 0;
};

} // namespace map_tiles
} // namespace ui
