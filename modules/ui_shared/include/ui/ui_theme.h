#pragma once

#include "ui/theme/theme_registry.h"

namespace ui::theme
{
inline lv_color_t page_bg() { return color(ColorSlot::PageBg); }
inline lv_color_t surface() { return color(ColorSlot::SurfacePrimary); }
inline lv_color_t surface_alt() { return color(ColorSlot::SurfaceSecondary); }
inline lv_color_t border() { return color(ColorSlot::BorderDefault); }
inline lv_color_t border_strong() { return color(ColorSlot::BorderStrong); }
inline lv_color_t separator() { return color(ColorSlot::SeparatorDefault); }
inline lv_color_t accent() { return color(ColorSlot::AccentPrimary); }
inline lv_color_t accent_strong() { return color(ColorSlot::AccentStrong); }
inline lv_color_t text() { return color(ColorSlot::TextPrimary); }
inline lv_color_t text_muted() { return color(ColorSlot::TextMuted); }
inline lv_color_t white() { return color(ColorSlot::TextInverse); }
inline lv_color_t status_green() { return color(ColorSlot::StateOk); }
inline lv_color_t status_blue() { return color(ColorSlot::StateInfo); }
inline lv_color_t battery_green() { return color(ColorSlot::StateOk); }
inline lv_color_t map_bg() { return color(ColorSlot::MapBackground); }
inline lv_color_t map_route() { return color(ColorSlot::MapRoute); }
inline lv_color_t map_track() { return color(ColorSlot::MapTrack); }
inline lv_color_t map_marker_border() { return color(ColorSlot::MapMarkerBorder); }
inline lv_color_t map_signal_label_bg() { return color(ColorSlot::MapSignalLabelBg); }
inline lv_color_t map_node_marker() { return color(ColorSlot::MapNodeMarker); }
inline lv_color_t map_self_marker() { return color(ColorSlot::MapSelfMarker); }
inline lv_color_t map_link() { return color(ColorSlot::MapLink); }
inline lv_color_t map_readout_id() { return color(ColorSlot::MapReadoutId); }
inline lv_color_t map_readout_lon() { return color(ColorSlot::MapReadoutLon); }
inline lv_color_t map_readout_lat() { return color(ColorSlot::MapReadoutLat); }
inline lv_color_t map_readout_distance() { return color(ColorSlot::MapReadoutDistance); }
inline lv_color_t map_readout_protocol() { return color(ColorSlot::MapReadoutProtocol); }
inline lv_color_t map_readout_rssi() { return color(ColorSlot::MapReadoutRssi); }
inline lv_color_t map_readout_snr() { return color(ColorSlot::MapReadoutSnr); }
inline lv_color_t map_readout_seen() { return color(ColorSlot::MapReadoutSeen); }
inline lv_color_t map_readout_zoom() { return color(ColorSlot::MapReadoutZoom); }
inline lv_color_t error() { return color(ColorSlot::StateError); }
} // namespace ui::theme
