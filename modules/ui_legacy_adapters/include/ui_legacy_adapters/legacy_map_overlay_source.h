#pragma once

#include "ui_map_runtime/map_overlay_snapshot_source.h"

namespace ui
{
namespace presentation_sources
{

using IMapOverlayGpsSource = ::ui::map_overlay::IMapOverlayGpsSource;
using IMapOverlayTeamSource = ::ui::map_overlay::IMapOverlayTeamSource;
using LegacyMapOverlaySource
    [[deprecated("Use ui::map_overlay::MapOverlaySnapshotSource")]] =
        ::ui::map_overlay::MapOverlaySnapshotSource;

} // namespace presentation_sources
} // namespace ui
