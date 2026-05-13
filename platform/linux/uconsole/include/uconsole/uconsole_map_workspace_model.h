#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "platform/linux/map_contour_tile_generator.h"
#include "platform/linux/map_tile_cache.h"
#include "ui/presentation_sources/legacy_gps_status_source.h"
#include "ui/presentation_sources/legacy_map_action_sink.h"
#include "ui/presentation_sources/legacy_map_presentation_source.h"
#include "ui_presentation/map/map_workspace_model.h"
#include "ui_presentation/map/map_workspace_snapshot.h"

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

struct MapContourTileItem
{
    ::platform::linux_runtime::MapContourTileId id{};
    std::filesystem::path path{};
    std::size_t base_tile_index = 0;
    bool available = false;
};

struct MapNodeOverlayItem
{
    std::uint32_t node_id = 0;
    std::string label{};
    double lat = 0.0;
    double lon = 0.0;
    double x_fraction = 0.0;
    double y_fraction = 0.0;
    bool via_mqtt = false;
    bool is_contact = false;
    std::uint32_t last_seen = 0;
    float rssi = 0.0F;
    float snr = 0.0F;
    std::uint8_t hops_away = 0xFF;
    std::uint8_t channel = 0xFF;
    bool has_altitude = false;
    std::int32_t altitude_m = 0;
};

struct MapCoordinate
{
    bool valid = false;
    double lat = 0.0;
    double lon = 0.0;
};

struct MapWorkspaceSnapshot
{
    ::ui::map::MapWorkspaceSnapshot presentation_workspace{};

    bool has_center = false;
    bool has_fix = false;
    bool has_configured_center = false;
    bool has_manual_center = false;
    bool using_default_center = false;
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
    std::size_t columns = 0;
    std::size_t rows = 0;
    std::size_t center_tile_index = 0;
    std::vector<MapTileItem> tiles{};
    bool contour_enabled = false;
    bool contour_ultra_fine_enabled = false;
    bool earthdata_token_configured = false;
    std::size_t contour_available_count = 0;
    std::size_t contour_missing_count = 0;
    std::vector<std::string> contour_profiles{};
    std::vector<MapContourTileItem> contour_tiles{};
    bool show_mqtt_nodes = true;
    std::size_t visible_node_count = 0;
    std::size_t visible_mqtt_node_count = 0;
    std::size_t hidden_mqtt_node_count = 0;
    std::vector<MapNodeOverlayItem> nodes{};
    ::platform::linux_runtime::MapTileCacheStats cache_stats{};
};

class UConsoleMapWorkspaceModel final
{
  public:
    explicit UConsoleMapWorkspaceModel(linux_app::LinuxAppServices& services);

    [[nodiscard]] MapWorkspaceSnapshot snapshot() const;
    [[nodiscard]] MapWorkspaceSnapshot snapshotAround(double lat,
                                                      double lon,
                                                      int zoom,
                                                      int radius_x,
                                                      int radius_y) const;
    [[nodiscard]] ::ui::map::MapWorkspaceModel& presentationModel() noexcept
    {
        return presentation_model_;
    }
    [[nodiscard]] const ::ui::map::MapWorkspaceModel& presentationModel() const
        noexcept
    {
        return presentation_model_;
    }
    [[nodiscard]] ::platform::linux_runtime::MapTileResult ensureTile(
        const ::platform::linux_runtime::MapTileId& tile) const;
    [[nodiscard]] ::platform::linux_runtime::MapContourGenerationResult
    ensureContourTiles(
        const std::vector<::platform::linux_runtime::MapContourTileId>& tiles)
        const;

    void setSource(::platform::linux_runtime::MapBaseSource source);
    void setZoom(int zoom);
    void setShowMqttNodes(bool enabled);
    void setContourEnabled(bool enabled);
    void setContourUltraFineEnabled(bool enabled);
    void setEarthdataToken(const std::string& token);
    [[nodiscard]] bool contourUltraFineEnabled() const;
    [[nodiscard]] std::string earthdataToken() const;
    [[nodiscard]] bool earthdataTokenConfigured() const;
    [[nodiscard]] MapCoordinate coordinateAtDisplayPoint(
        const MapWorkspaceSnapshot& snapshot,
        double display_x,
        double display_y,
        int display_width,
        int display_height) const;
    void centerOn(double lat, double lon, bool persist);
    void zoomInAt(double lat, double lon);
    void zoomOutAt(double lat, double lon);
    void panByDisplayDelta(double drag_dx,
                           double drag_dy,
                           int display_width,
                           int display_height,
                           double start_lat,
                           double start_lon,
                           int start_zoom,
                           bool persist);
    void clearManualCenter();
    void zoomIn();
    void zoomOut();

  private:
    [[nodiscard]] ::platform::linux_runtime::MapBaseSource source() const;
    [[nodiscard]] bool showMqttNodes() const;
    [[nodiscard]] bool contourEnabled() const;
    void syncPresentationWorkspace(double lat, double lon, int zoom) const;
    void persistZoom() const;
    void persistManualCenter() const;
    void clearPersistedManualCenter() const;

    linux_app::LinuxAppServices& services_;
    mutable ::ui::presentation_sources::LegacyGpsStatusSource legacy_gps_source_{};
    mutable ::ui::presentation_sources::LegacyMapPresentationState
        legacy_map_state_{};
    mutable ::ui::presentation_sources::LegacyMapPresentationSource
        legacy_map_source_;
    mutable ::ui::presentation_sources::LegacyMapActionSink legacy_map_sink_;
    mutable ::ui::map::MapWorkspaceModel presentation_model_;
    ::platform::linux_runtime::MapTileCache tile_cache_{};
    ::platform::linux_runtime::MapContourTileStore contour_store_{};
    ::platform::linux_runtime::MapContourTileGenerator contour_generator_{};
    int zoom_ = 14;
    bool manual_center_active_ = false;
    double manual_center_lat_ = 0.0;
    double manual_center_lon_ = 0.0;
};

} // namespace trailmate::uconsole
