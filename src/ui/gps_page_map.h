#ifndef GPS_PAGE_MAP_H
#define GPS_PAGE_MAP_H

// Map tile management and updates
void update_map_tiles(bool lightweight = false);
void update_map_anchor();
void tick_loader();
void tick_gps_update(bool allow_map_refresh);

#endif // GPS_PAGE_MAP_H
