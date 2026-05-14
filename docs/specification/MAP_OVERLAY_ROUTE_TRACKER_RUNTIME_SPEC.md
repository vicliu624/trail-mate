# Map Overlay / Route / Tracker Runtime Spec

## Ownership Rule

Map overlay state is presentation projection state. It is not tile source state,
not renderer widget state, and not filesystem state.

The required flow is:

runtime source -> presentation source / projector -> `MapOverlaySnapshot` -> renderer

The forbidden flow is:

renderer -> GPS runtime / Team store / route store / tracker store / filesystem

## Types

`MapOverlayKind` describes what a map item means:

- `CurrentPosition`
- `TeamMember`
- `RoutePoint`
- `TrackPoint`
- `MeasurementPoint`
- `SelectedTarget`
- `Warning`

`MapOverlayStyle` describes renderer styling intent:

- `Default`
- `OwnPosition`
- `Team`
- `Route`
- `Track`
- `Warning`

`MapOverlayItem` is a view DTO with:

- kind/style
- `MapGeoPoint`
- label/detail fixed text
- stable id
- icon id
- selected/visible flags

`MapOverlaySnapshot` is a fixed-capacity snapshot with `kMaxItems = 64`.

It must not contain:

- `lv_obj_t*`
- tile paths
- bitmap bytes
- route store pointers
- GPS source pointers
- Team store pointers
- filesystem paths

## Source Port

`IMapOverlayPresentationSource` owns read projection:

```cpp
virtual bool buildMapOverlaySnapshot(MapOverlaySnapshot& out) const = 0;
```

It may be implemented by a legacy adapter, but renderers must depend on the port
or on an already-built snapshot, not on concrete runtime sources.

## Projector

`MapOverlayProjector` maps already-read runtime facts to overlay rows:

- `projectCurrentPosition(...)`
- `projectTeamMember(...)`

It validates coordinates, assigns kind/style/stable id, and appends to the fixed
snapshot.

It must not:

- read GPS runtime
- read Team store
- render LVGL widgets
- access tile source/cache
- access filesystem

## Legacy Adapter

`LegacyMapOverlaySource` is an anti-corruption adapter. It may depend on:

- `IMapOverlayGpsSource`
- `IMapOverlayTeamSource`
- `MapOverlayProjector`

The adapter exists to contain legacy GPS/team fact gathering. It does not render,
open files, or mutate map viewport state.

## Runtime Wiring

Map page runtime or composition root owns:

- concrete legacy GPS/team overlay sources
- `LegacyMapOverlaySource`
- built overlay snapshot

Renderer consumes the overlay snapshot. During migration, a runtime may build
the snapshot before render and keep renderer changes small.

## Deferred Items

Route/tracker/breadcrumb overlays are deferred until route and tracker stores
have presentation source ports. Exit condition:

route/tracker facts are projected into `MapOverlaySnapshot` or a dedicated route
overlay snapshot, and renderer code no longer reads concrete route/tracker
stores.

Measurement point overlays are deferred until the measurement tool has a
runtime/source boundary. Exit condition:

measurement state is projected as `MeasurementPoint` items and renderers consume
the same overlay snapshot path.

## Non-goals

- No map overlay visual redesign.
- No online tile download.
- No route/tracker engine rewrite.
- No measurement tool rewrite.
- No UX Pack work.
- No repository layout work.
