# Map Presentation Boundary Audit

## Purpose

Phase 5.7 establishes a UI presentation boundary for map workspace state.

Phase 5.7 is `MapWorkspaceModel minimal`, not a map engine rewrite.

It does not rewrite tile loading, tile cache, route planning, POI, contour
generation, Team marker rendering, or rich map rendering.

The first goal is to separate portable workspace state from legacy runtime
mechanics:

- self location
- viewport
- layer state
- center-on-self action
- selected map tool
- measuring mode state
- basic Team overlay summary

## Boundary Rule

Renderer must not directly read:

- `platform/ui/gps_runtime.h`
- GPS drivers
- tile cache
- SD card map store
- `TeamUiStore` raw state
- platform map probes
- contour tile generators
- route/track storage

Renderer should consume:

- `MapWorkspaceModel::snapshot()`
- `MapWorkspaceModel` actions
- MapWorkspaceModel actions

Legacy GPS/map/runtime reads are allowed only inside compatibility
presentation-source adapters.

## Migrated in Phase 5.7

- `ui_presentation/map` now contains the portable map workspace snapshot,
  source port, action sink port, and ViewModel.
- `LegacyMapPresentationSource` and `LegacyMapActionSink` now bound legacy
  GPS/map/team compatibility state behind read/write adapters.
- The compact LVGL map runtime consumes `MapWorkspaceModel` for self-location
  rendering and center-on-self action flow.
- `UConsoleMapWorkspaceModel` now exposes a portable
  `presentation_workspace` and consumes `LegacyGpsStatusSource` instead of
  including `platform/ui/gps_runtime.h` directly in its map center path.
- The ASCII/headless probe renders `MapWorkspaceSnapshot` without LVGL, GTK,
  GPS runtime, tile cache, or raw Team store dependencies.

## Current Consumers

| Consumer | Current source | Target source | Phase owner |
| --- | --- | --- | --- |
| LVGL GPS/map page | `GPSPageState`, `platform/ui/gps_runtime.h`, `ui/widgets/map/map_tiles.h`, `TeamUiStore`, route/tracker overlays | `MapWorkspaceModel` for workspace state; legacy renderer/tile engine unchanged | 5.7-c partial |
| Shared LVGL map viewport widget | `ui/widgets/map/map_viewport.*` reads app config, device runtime, and platform map tiles while owning LVGL tile/gesture runtime | renderer consumes `MapWorkspaceSnapshot`; widget remains tile renderer/runtime until later | Later renderer cleanup |
| LVGL GPS title/status overlay | `gps_page_map.cpp` reads `gps_runtime`, device runtime, config, and map tile state | `MapWorkspaceSnapshot.self`, `layers`, and `status_line` | 5.7-c partial |
| LVGL Team map overlay | `TeamUiStore`, `TeamUiSnapshot`, Team position ring, Team chat log location markers, contact service, LVGL marker objects | `MapWorkspaceSnapshot.team` summary only in 5.7; rich markers remain legacy | 5.7-c summary / later rich overlay |
| LVGL tracker/route overlays | page-local route/tracker state and `route_storage` helpers | unchanged | Later |
| GTK/uConsole map model | `UConsoleMapWorkspaceModel` reads `gps_runtime`, settings store, Linux tile cache, contour store/generator, contacts, map env vars | `MapWorkspaceModel` for portable workspace state; uConsole tile model remains legacy renderer adapter | 5.7-d partial |
| GTK/uConsole map renderer | GTK logic/layout consume `UConsoleMapWorkspaceModel` snapshots with tile paths, contour tiles, node projections, cache stats | consume portable snapshot for status/actions; keep tile rendering path legacy | 5.7-d partial |
| ASCII/headless map probe | `uconsole_map_workspace_smoke` directly exercises `UConsoleMapWorkspaceModel` | portable `MapWorkspaceSnapshot` smoke output | 5.7-e |
| Tile renderer/cache | ESP/Linux `map_tiles`, Linux `MapTileCache`, SD/cache paths | unchanged | Later |
| Contour generation | Linux `MapContourTileStore` / `MapContourTileGenerator`, Earthdata token settings | unchanged | Later |
| Measurement tools | no portable model; pan/zoom/edit state is page/model local | `MapWorkspaceModel` tool state only | 5.7-a/c partial |

## Current State Locations

| State | Current owner | Notes |
| --- | --- | --- |
| self location | `platform/ui/gps_runtime.h`; uConsole env fallbacks; ESP GPS runtime | full GPS state belongs to GPS presentation, not Map; Map needs only overlay facts |
| viewport center | LVGL `GPSPageState.lat/lng/pan_x/pan_y/follow_position`; uConsole `manual_center_*` and `zoom_` | 5.7 may let `MapWorkspaceModel` own presentation-local viewport |
| zoom | LVGL `GPSPageState.zoom_level`; uConsole `zoom_` persisted in settings | portable model should expose zoom, not tile coordinates |
| layer state | app config map source / contour; uConsole settings for contour ultra-fine and MQTT nodes | 5.7 layer state means UI-enabled state, not tile availability |
| tile loading | `ui/widgets/map/map_tiles.*`, Linux `MapTileCache`, platform SD/cache paths | explicitly outside 5.7 |
| offline map directory | map source directory helpers and SD card paths | explicitly outside 5.7 |
| Team overlay | LVGL Team position ring/chat log markers; uConsole contact/node projections | 5.7 exposes summary only |
| measurement/tool state | LVGL `GpsEditMode`; uConsole pan/zoom helpers; no portable measurement owner | 5.7 only freezes active tool / simple measurement state |
| touch/keyboard/encoder input | LVGL event handlers and GTK logic | renderer/input layer sends actions to model, gesture engine unchanged |

## Phase 5.7 Scope

Phase 5.7 owns:

- `ui_presentation/map` snapshot/source/sink/model
- self location overlay in `MapWorkspaceSnapshot`
- presentation-local viewport state
- layer enabled state
- center-on-self action boundary
- selected tool state
- measurement state placeholder
- basic Team overlay summary
- at least one LVGL/GTK/ASCII consumer of `MapWorkspaceModel`

Phase 5.7 does not own:

- tile image loading
- tile cache layout or eviction
- SD card map package scanning
- offline map directory repair
- route planning
- POI database
- contour tile generation
- rich Team marker rendering
- touch gesture engine
- advanced measure/area/track tools

## Legacy Island Notes

### LVGL GPS/map page

`GPSPageState` is a legacy page state object. It currently mixes:

- LVGL widgets
- GPS/self location
- tile context and map anchors
- pan/zoom/follow state
- route/tracker overlays
- Team member markers
- Team signal markers
- input/edit mode
- timers and lifetime state

It must not be treated as a portable snapshot source. Phase 5.7 should wrap the
parts needed for workspace status/actions and leave tile rendering and rich
overlays as legacy-owned.

### uConsole map model

`UConsoleMapWorkspaceModel` is a platform-specific application model, not the
new portable `ui_presentation` model. It currently owns:

- GPS runtime fallback logic
- configured/manual center rules
- tile id and path enumeration
- tile cache stats
- contour store/generator access
- settings persistence
- MQTT/contact node projection
- click-to-coordinate math
- pan and zoom commands

Phase 5.7 can bridge from it, but must not copy its tile/cache-heavy snapshot
shape into `ui_presentation/map`.

## Explicit Non-goals

- no tile engine rewrite
- no map cache rewrite
- no SD card scanning rewrite
- no route planning
- no rich Team marker payload
- no advanced measurement tools
- no GTK/LVGL renderer rewrite
- no GPS runtime rewrite
- no `UConsoleMapWorkspaceModel` deletion

## Acceptance

1. Current map/GPS/team/tile consumers are listed.
2. Phase 5.7 scope is limited to workspace presentation state/actions.
3. Tile/cache/renderer/route/rich overlay ownership remains explicitly
   deferred.
4. Legacy map/GPS reads are allowed only behind presentation-source adapters.
5. Portable `ui_presentation/map` must not derive from LVGL `GPSPageState` or
   uConsole tile/cache snapshots.
