# Presentation Workspace Specification

## Purpose

`PresentationWorkspace` is a typed composition object for presentation models.

It groups presentation models that a shell or target may consume:

- `DeviceStatusModel`
- `GpsStatusModel`
- `MeshStatusModel`
- `SettingsModel`
- `ChatWorkspaceModel`
- Team `ChatWorkspaceModel`
- `MapWorkspaceModel`

Phase 5.8 uses this object to prove that the product presentation graph can be
consumed by LVGL, uConsole, and ASCII/headless surfaces without each renderer
directly assembling business or platform state.

## Pattern

This boundary follows:

- Composition Object
- Typed Presentation Graph
- Optional Capability Set
- Headless Probe Boundary

`PresentationWorkspace` is not:

- service locator
- dependency injection container
- app service registry
- platform composition root
- renderer
- snapshot cache
- telemetry service

## Ownership

`PresentationWorkspace` does not own:

- source objects
- sink objects
- app services
- platform runtimes
- renderers
- threads
- tasks
- event loops

Targets own object lifetime and construction order.

The workspace may hold pointers to already-constructed presentation models. It
must not construct models, sources, sinks, services, adapters, or platform
runtimes.

## Nullability

All fields are nullable.

A target may expose only the presentation capabilities it supports.

Examples:

- a headless target may expose Device/GPS/Mesh only
- compact LVGL may expose Device/GPS/Mesh/Chat/Map
- desktop GTK may expose Device/GPS/Mesh/Settings/Chat/Team Chat/Map

Null does not mean failure. It means the capability is not present in that
target composition.

## Snapshot

`PresentationWorkspaceProbe` may build a lightweight composite snapshot.

The composite snapshot is for readiness and probe output only. It must not
replace individual workspace snapshots.

The composite snapshot may include small status snapshots and map workspace
summary:

- `DeviceStatusSnapshot`
- `GpsStatusSnapshot`
- `MeshStatusSnapshot`
- `MapWorkspaceSnapshot`

It must not include:

- full `ChatWorkspaceSnapshot`
- full Team chat snapshot
- full `SettingsSnapshot`
- tile cache state
- raw Team payloads
- platform runtime state

Settings, Chat, and Team Chat are represented as capability flags in the
composite snapshot. Renderers that need those details must read the specific
model snapshot directly.

## Boundary

`ui_presentation/workspace` may include existing presentation model headers.

It must not include or depend on:

- platform headers
- LVGL or GTK headers
- `ChatService`
- `ContactService`
- `GpsService`
- `MeshSession`
- `MapTileCache`
- stores
- radio drivers
- SQLite
- preferences backends

## Action Semantics

`PresentationWorkspaceProbe` is read-only.

It may call `snapshot()` on contained presentation models. It must not call
actions such as send, select, mark read, center-on-self, layer toggle, or
settings apply.

The workspace itself does not expose dynamic lookup. Callers should use typed
fields:

```text
workspace.gps->snapshot()
workspace.map->centerOnSelf()
workspace.chat->snapshot()
```

They must not use the workspace as a name-based service locator or route from
it into business services.
