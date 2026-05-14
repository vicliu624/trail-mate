# Phase 7.12 Map Overlay / Route / Tracker Report

## Burned Down

| Surface | Outcome |
| --- | --- |
| Current GPS overlay semantic state | Added `MapOverlaySnapshot` / `MapOverlayItem` and `MapOverlayProjector::projectCurrentPosition(...)` |
| Team member overlay semantic state | Added `MapOverlayProjector::projectTeamMember(...)` |
| Renderer-driven runtime reads | Shared map viewport remains free of GPS/team store reads; checker guards the boundary |
| Legacy overlay fact gathering | Added `LegacyMapOverlaySource` with `IMapOverlayGpsSource` and `IMapOverlayTeamSource` ports |

## Runtime Wiring

The Linux GPS page runtime now builds a `MapOverlaySnapshot` through
`LegacyMapOverlaySource` at the runtime/composition boundary. This keeps shared
map rendering on the passive side of the boundary while preserving current
visual behavior.

## Still Contained

| Surface | Removal condition |
| --- | --- |
| ESP route/tracker draw callbacks | Route/tracker stores expose presentation sources and renderer consumes overlay snapshot rows |
| ESP GPS marker widget records | Dedicated map overlay renderer consumes `MapOverlaySnapshot` and owns marker widget lifecycle |
| Measurement overlay | Measurement tool state is projected as `MeasurementPoint` items |
| Team signal markers from chatlog | Team signal source becomes an overlay presentation source or is folded into Team location projection |

## Checker Result

Phase 7 checker now requires the 7.12 docs, overlay DTO/source, projector, legacy
adapter, and projector tests. Shared map renderer code is guarded against direct
GPS/team source reads.
