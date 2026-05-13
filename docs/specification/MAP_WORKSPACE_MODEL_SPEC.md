# Map Workspace Model Specification

## Purpose

`MapWorkspaceModel` is a presentation-layer workspace ViewModel.

It owns UI-local map workspace state only:

- requested viewport
- active map tool

It does not own:

- GPS runtime
- TimeAuthority
- tile loading
- tile cache
- SD card map package scanning
- offline map directory repair
- map renderer objects
- route planning
- POI storage
- rich Team marker state
- touch gesture engine
- retry state for tile downloads

## Pattern

This model follows:

- MVVM / Passive View
- Source/Sink Port
- Immutable Snapshot
- CQRS Read/Command split
- Compatibility Adapter boundary for legacy map runtime

## Snapshot Flow

Renderer calls:

```text
MapWorkspaceModel::snapshot()
    -> IMapPresentationSource::buildMapWorkspaceSnapshot(request, out)
```

`MapWorkspaceRequest` contains:

- requested viewport
- active tool

The returned `MapWorkspaceSnapshot` is a value object for rendering map
workspace state. It is not a tile engine snapshot.

## Action Flow

Renderer calls:

```text
MapWorkspaceModel::centerOnSelf()
MapWorkspaceModel::setViewport(viewport)
MapWorkspaceModel::setLayer(layer, enabled)
MapWorkspaceModel::setActiveTool(tool)
MapWorkspaceModel::clearMeasurement()
```

The model forwards actions to `IMapActionSink`.

## Center On Self

`centerOnSelf()` uses confirmed update semantics.

Semantics:

1. Notify `IMapActionSink::centerOnSelf()`.
2. If the sink fails, keep the current viewport unchanged.
3. If the sink succeeds, request a fresh snapshot.
4. If the snapshot has a valid self-location overlay, update the local viewport
   center to that location.

This differs from Chat optimistic selection. Chat selection is UI-local.
Map center-on-self depends on a real GPS fix and must not pretend success when
self location is unavailable.

## Viewport

In Phase 5.7, viewport is presentation-local state.

Legacy renderers may still own pan offsets, tile anchors, and gesture details.
Those are renderer/runtime details and are not part of `MapViewport`.

`MapViewport` expresses:

- center latitude
- center longitude
- zoom

It does not express:

- tile x/y
- tile file path
- pan pixel offset
- gesture delta
- map widget anchor state

## Layer State

`MapLayerState` expresses UI-enabled layer state only.

It does not guarantee:

- tile availability
- SD card availability
- cache hit state
- map package validity
- contour generation status

Tile availability remains owned by the legacy tile/cache runtime until a later
map engine phase.

## Self Location

`SelfLocationOverlay` contains only map drawing facts:

- valid
- latitude
- longitude
- course
- accuracy

Full GPS receiver status belongs to `GpsStatusModel`, not
`MapWorkspaceModel`.

## Measurement

`MeasurementState` is a Phase 5.7 placeholder for simple presentation state.

Phase 5.7 does not implement:

- advanced distance tools
- area tools
- route tracing
- persisted measurement history

## Team Overlay

`TeamOverlaySummary` is a projection summary only.

TeamOverlaySummary is a projection summary only.

It may expose:

- whether Team overlay data is available
- visible member count
- stale member count

It must not expose:

- raw `TeamUiStore` state
- Team chat payloads
- Team position ring samples
- marker widget objects
- rich Team location/command payloads

Rich Team map markers require a later bounded map overlay presentation phase.

## Legacy Adapters

`LegacyMapPresentationSource` is a read adapter. It projects legacy GPS/map/team
compatibility surfaces into `MapWorkspaceSnapshot`.

It must not:

- load map tiles
- mutate viewport state
- access LVGL widgets
- open SD card tile paths
- perform route planning
- expose Team raw payloads

`LegacyMapActionSink` is a command adapter. It handles presentation-level map
actions.

It must not:

- build `MapWorkspaceSnapshot`
- touch LVGL widgets
- load tile images
- parse GPS data
- write map storage directly
- perform radio or packet work

## Renderer Rule

LVGL, GTK, ASCII, and future renderers should consume:

```text
MapWorkspaceModel::snapshot()
MapWorkspaceModel actions
```

Renderer code must not add new direct reads of:

- GPS runtime
- tile cache
- SD card map store
- raw Team store
- platform map probes

Existing legacy reads are bounded debt and must be listed in the map legacy
ownership audit.

## Phase 5.7 Completion

Phase 5.7 establishes the presentation boundary.

It does not make the map stack clean.

Tile/cache/renderer/route/rich overlay cleanup remains future work.
