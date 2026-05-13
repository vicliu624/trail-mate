# Map UI Legacy Ownership Audit

## Current Role

Map UI is still a legacy island.

Phase 5.7 introduced a portable presentation boundary, but existing map
renderers and platform models still own substantial runtime behavior.

## Migrated in Phase 5.7

The following surfaces now have a `MapWorkspaceModel` boundary:

- `ui_presentation/map` defines `MapWorkspaceSnapshot`,
  `IMapPresentationSource`, `IMapActionSink`, and `MapWorkspaceModel`.
- `LegacyMapPresentationSource` projects GPS self-location, layer state,
  measurement placeholder state, and Team overlay summary.
- `LegacyMapActionSink` handles center-on-self, viewport, layer, active tool,
  and clear-measurement actions.
- The compact LVGL map runtime consumes `MapWorkspaceModel::snapshot()` for
  self-location focus and routes center-on-self through
  `MapWorkspaceModel::centerOnSelf()`.
- `UConsoleMapWorkspaceModel` exposes a portable
  `presentation_workspace` snapshot while preserving its legacy tile/cache
  snapshot.
- `UConsoleMapWorkspaceModel` no longer includes `platform/ui/gps_runtime.h`
  directly for its map center fix path; it consumes `LegacyGpsStatusSource`.
- `trailmate_map_workspace_ascii_probe` renders a headless
  `MapWorkspaceSnapshot` without LVGL, GTK, GPS runtime, tile cache, or Team
  raw store dependencies.

## Remaining Legacy Ownership

The following remain legacy-owned:

- ESP/LVGL `GPSPageState` still owns LVGL widgets, tile context, map anchors,
  pan/zoom/follow state, route/tracker overlays, Team marker widgets, timers,
  and input/edit mode.
- The shared LVGL `ui/widgets/map/map_viewport.*` runtime still owns tile
  rendering, tile loader timers, gesture runtime, map source config reads, and
  tile availability checks.
- ESP/Linux map tile loaders still own tile image loading, SD/cache paths,
  contour tile rendering, and missing tile notices.
- GTK/uConsole still owns tile path enumeration, cache stats, contour store and
  generator access, MQTT/contact node projection, click-to-coordinate math,
  and pan/zoom persistence.
- Rich Team map markers remain legacy-owned through Team position ring and
  Team chat location marker paths.
- Route/tracker overlays remain legacy-owned.
- Advanced measurement tools remain legacy-owned or unimplemented.
- Offline map package scanning and repair remain legacy-owned.
- Tile download retry state remains legacy-owned.

## Constraint

New map workspace status and actions should prefer `MapWorkspaceModel`.

New direct renderer reads of GPS runtime, tile cache, SD map store, raw Team
store, or platform map probes must either be moved behind a presentation source
or explicitly documented as legacy ownership.

## Renderer Hardening in Phase 5.9

Phase 5.9 locks the migrated Map paths without rewriting the tile renderer.

Regression guard:

- compact LVGL map status must continue using
  `map_workspace_model().snapshot()`
- compact LVGL center-on-self must continue using
  `map_workspace_model().centerOnSelf()`
- uConsole map status/center summary must continue exposing
  `presentation_workspace`
- uConsole map center fix path must not reintroduce a direct
  `platform/ui/gps_runtime.h` include

Allowed legacy ownership remains:

- LVGL `GPSPageState`
- `ui/widgets/map/map_viewport`
- tile/cache/contour loading and rendering
- route/tracker overlays
- rich Team map markers
- uConsole tile/cache/contour platform model

## Non-goal

Phase 5.7 does not remove:

- `GPSPageState`
- `ui/widgets/map/map_viewport`
- `UConsoleMapWorkspaceModel`
- tile cache/runtime objects
- route/tracker overlay code
- Team rich marker code

Those require later slices with separate rendering, tile engine, and overlay
presentation decisions.
