#pragma once

#include "tmap/reader.h"
#include "ui_map_runtime/map_tiles/map_tile_source.h"

namespace ui::map_tiles
{
// All methods run on one storage worker. The allocator must use PSRAM on ESP,
// without an internal-RAM fallback. Enumeration must preserve position on Busy.
class TmapStorage : public tmap::RandomAccessFile
{
  public:
    virtual void* allocate(size_t bytes, size_t alignment) = 0;
    virtual void release(void* memory) = 0;
    virtual uint32_t session() const = 0;
    virtual tmap::Status nextPackage(char* path, size_t capacity) = 0;
    virtual tmap::Status openPackage(const char* path) = 0;
    virtual void closePackage() = 0;
    virtual void resetEnumeration() = 0;
};

// One reader/cache for every package. The catalog grows in blocks of 16 entries,
// with an explicit maximum; capacity overflow is an error, never silent omission.
class TmapMapTileSource final : public IMapTileSource
{
  public:
    static constexpr size_t kMaxPackages = 256;
    explicit TmapMapTileSource(TmapStorage& storage) : storage_(storage) {}
    ~TmapMapTileSource() override;
    TmapMapTileSource(const TmapMapTileSource&) = delete;
    TmapMapTileSource& operator=(const TmapMapTileSource&) = delete;
    MapTileLookupResult lookup(const MapTileRef& ref) const override;
    MapTileReadResult read(const MapTileRef& ref, uint8_t* buffer, size_t capacity) const override;
    void reset();
    size_t allocatedBytes() const;
    size_t packageCount() const;

  private:
    struct State;
    TmapStorage& storage_;
    mutable State* state_ = nullptr;
    tmap::Status catalog() const;
    tmap::Status select(const MapTileRef& ref, tmap::Tile& tile) const;
    MapTileReadResult annotations(const MapTileRef& ref, uint8_t* output, size_t capacity) const;
};
} // namespace ui::map_tiles
