#ifndef GPS_PAGE_MAP_H
#define GPS_PAGE_MAP_H

namespace gps
{
struct GpsState;
}

// Map tile management and updates
void update_map_tiles(bool lightweight = false);
void update_map_anchor();
void tick_loader();
void tick_gps_update(bool allow_map_refresh);

// GPS marker management
void update_gps_marker_position();
void create_gps_marker();
void hide_gps_marker();

// Team markers (posring)
void refresh_team_markers_from_posring();
void update_team_marker_positions();
void clear_team_markers();
void refresh_team_signal_markers_from_chatlog();
void update_team_signal_marker_positions();
void clear_team_signal_markers();
void refresh_member_panel(bool force = false);

// UI status and title updates (moved from presenter)
void update_title_and_status();
void update_resolution_display();
void update_altitude_display(const gps::GpsState& gps_data);
void reset_title_status_cache(); // Reset cached state to force next update
void update_zoom_btn();          // Placeholder for future zoom button updates

// Coordinate system transform for map display (WGS84 -> GCJ/BD if configured)
void gps_map_transform(double lat, double lon, double& out_lat, double& out_lon);

#endif // GPS_PAGE_MAP_H
