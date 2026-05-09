#pragma once

#include "ui/widgets/map/map_viewport.h"

namespace gps_ui
{

constexpr int kMapPanStep = 32;
constexpr int kDefaultZoom = ::ui::widgets::map::kDefaultZoom;
constexpr int kMinZoom = ::ui::widgets::map::kMinZoom;
constexpr int kMaxZoom = ::ui::widgets::map::kMaxZoom;

constexpr double kDefaultLat = 51.5074;
constexpr double kDefaultLng = -0.1278;

constexpr bool kShowLoadingOverlay = false;

} // namespace gps_ui
