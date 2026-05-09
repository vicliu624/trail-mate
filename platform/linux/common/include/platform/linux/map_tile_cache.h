#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace platform::linux_runtime
{

enum class MapBaseSource : std::uint8_t
{
    Osm = 0,
    Terrain = 1,
    Satellite = 2,
};

struct MapTileId
{
    MapBaseSource source = MapBaseSource::Osm;
    int z = 0;
    int x = 0;
    int y = 0;
};

enum class MapTileStatus
{
    Invalid,
    Missing,
    Cached,
    Downloaded,
    Failed,
};

struct MapTileResult
{
    MapTileStatus status = MapTileStatus::Invalid;
    MapTileId tile{};
    std::filesystem::path path{};
    std::string message{};
    long http_status = 0;
    std::uintmax_t bytes = 0;
};

struct MapTileCacheStats
{
    std::filesystem::path root{};
    std::filesystem::path database{};
    std::uint64_t cached_tiles = 0;
    std::uint64_t failed_tiles = 0;
    std::uint64_t total_bytes = 0;
};

MapBaseSource sanitize_map_base_source(std::uint8_t source) noexcept;
const char* map_base_source_key(MapBaseSource source) noexcept;
const char* map_base_source_label(MapBaseSource source) noexcept;
const char* map_base_source_extension(MapBaseSource source) noexcept;

class MapTileCache final
{
  public:
    MapTileCache();
    explicit MapTileCache(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] std::filesystem::path tile_path(const MapTileId& tile) const;
    [[nodiscard]] std::filesystem::path relative_tile_path(
        const MapTileId& tile) const;
    [[nodiscard]] bool tile_available(const MapTileId& tile) const;
    [[nodiscard]] MapTileCacheStats stats() const;

    MapTileResult ensure_tile(const MapTileId& tile) const;

  private:
    std::filesystem::path root_{};
};

void normalize_map_tile(MapTileId& tile) noexcept;
std::vector<MapTileId> map_tiles_around(double lat,
                                        double lon,
                                        int zoom,
                                        MapBaseSource source,
                                        int radius_x,
                                        int radius_y);

} // namespace platform::linux_runtime
