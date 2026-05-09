#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "platform/linux/map_tile_cache.h"

namespace trailmate::linux_app
{
class LinuxAppServices;
}

namespace trailmate::uconsole
{

struct MapTileItem
{
    ::platform::linux_runtime::MapTileId id{};
    std::filesystem::path path{};
    bool available = false;
};

struct MapWorkspaceSnapshot
{
    bool has_fix = false;
    bool has_configured_center = false;
    double lat = 0.0;
    double lon = 0.0;
    double altitude_m = 0.0;
    bool has_altitude = false;
    double speed_mps = 0.0;
    bool has_speed = false;
    std::uint8_t satellites = 0;
    int zoom = 14;
    std::string source_label{};
    std::string fix_label{};
    std::vector<MapTileItem> tiles{};
    ::platform::linux_runtime::MapTileCacheStats cache_stats{};
};

class UConsoleMapWorkspaceModel final
{
  public:
    explicit UConsoleMapWorkspaceModel(linux_app::LinuxAppServices& services);

    [[nodiscard]] MapWorkspaceSnapshot snapshot() const;
    [[nodiscard]] ::platform::linux_runtime::MapTileResult ensureTile(
        const ::platform::linux_runtime::MapTileId& tile) const;

    void setSource(::platform::linux_runtime::MapBaseSource source);
    void zoomIn();
    void zoomOut();

  private:
    [[nodiscard]] ::platform::linux_runtime::MapBaseSource source() const;
    void persistZoom() const;

    linux_app::LinuxAppServices& services_;
    ::platform::linux_runtime::MapTileCache tile_cache_{};
    int zoom_ = 14;
};

} // namespace trailmate::uconsole
