#pragma once

#include "ui_map_runtime/map_tiles/map_tile_resolver.h"
#include "ui_map_runtime/map_tiles/map_tile_source.h"

#include <cstddef>

namespace ui
{
namespace map_tiles
{

class FilesystemMapTileSource final : public IMapTileSource
{
  public:
    FilesystemMapTileSource(IMapTileFileSystem& file_system,
                            const char* root_prefix = "");

    MapTileLookupResult lookup(const MapTileRef& ref) const override;

    bool read(const MapTileRef& ref,
              uint8_t* buffer,
              std::size_t capacity,
              std::size_t& out_size,
              MapTileFormat& out_format) const override;

    bool resolvePath(const MapTileRef& ref, char* out_path, std::size_t out_size) const;
    bool resolveDirectory(MapTileLayer layer, char* out_path, std::size_t out_size) const;
    bool layerDirectoryAvailable(MapTileLayer layer) const;
    bool anyContourDirectoryAvailable() const;

  private:
    IMapTileFileSystem& file_system_;
    MapTileResolver resolver_;
};

using LegacyFilesystemMapTileSource [[deprecated("Use FilesystemMapTileSource")]] =
    FilesystemMapTileSource;

} // namespace map_tiles
} // namespace ui
