# Map Overlay / Route / Tracker Ownership Audit

## Purpose

Phase 7.12 separates map overlay semantic state from tile source/cache and
from renderer widget lifecycle.

The ownership boundary is:

runtime source -> presentation source / projector -> overlay snapshot -> renderer -> LVGL / GTK / ASCII

Renderer code must not pull GPS runtime, Team store, route store, tracker store,
tile source, or filesystem state directly.

## Current State

| Question | Current owner before 7.12 | 7.12 owner |
| --- | --- | --- |
| Current GPS marker facts | ESP GPS page/map runtime reads `platform::ui::gps::get_data()`; Linux map workspace reads `LegacyGpsStatusSource` | `LegacyMapOverlaySource` via `IMapOverlayGpsSource` |
| Team member marker facts | ESP GPS page/map runtime reads Team posring/chatlog directly; Linux map workspace had only summary counts | `LegacyMapOverlaySource` via `IMapOverlayTeamSource` |
| Route / tracker / breadcrumb facts | ESP route/tracker overlay code still owns platform-specific drawing state | Explicitly deferred with exit condition |
| Measurement state | `LegacyMapPresentationState` / `MapWorkspaceSnapshot::measurement` | Deferred overlay projection until measurement tool runtime is split |
| Renderer access to GPS/team | `map_viewport` does not read GPS/team; ESP GPS map page still has contained legacy marker paths | Shared map renderer remains forbidden from reading GPS/team; ESP marker legacy remains contained |

## GPS Current Position Marker

Current GPS overlay is now expressible as `MapOverlayKind::CurrentPosition` in
`MapOverlaySnapshot`. `MapOverlayProjector::projectCurrentPosition(...)`
validates coordinate range and appends an own-position item with stable id.

The projector does not know about:

- `platform::ui::gps::get_data()`
- LVGL objects
- map tiles
- route/tracker stores

## Team Member Marker

Team member overlay is now expressible as `MapOverlayKind::TeamMember`.
`MapOverlayProjector::projectTeamMember(...)` accepts already-read runtime facts:

- node id
- label
- lat/lon
- validity

The Linux GPS page runtime wires a legacy team source that reads latest posring
samples at the composition/runtime boundary, then projects them through
`LegacyMapOverlaySource`.

## Route / Tracker / Breadcrumb

Route/tracker overlay is not rewritten in 7.12. It remains contained because the
current ESP page has LVGL draw callbacks and platform-specific route/tracker
state. The removal condition is:

`IMapOverlayPresentationSource` or a route/tracker-specific presentation source
can provide route and track items as `MapOverlaySnapshot` rows, and renderers
consume those rows without reading route/tracker stores.

## UI Tool Local State

Measurement and selected-object state can become overlay items, but 7.12 does
not move the measurement tool. Until that split, `MapWorkspaceSnapshot` remains
the compatibility read DTO for active tool and measurement summary.

## Burned Down

- Overlay semantic DTO exists outside renderer code.
- Current GPS overlay projection exists outside renderer code.
- Team member overlay projection exists outside renderer code.
- Shared map renderer is guarded against direct GPS/team store reads.

## Still Contained

- ESP route/tracker draw callbacks remain platform-specific contained legacy.
- ESP GPS page marker widgets still draw existing marker objects.
- Measurement overlay projection is deferred with an explicit exit condition.
