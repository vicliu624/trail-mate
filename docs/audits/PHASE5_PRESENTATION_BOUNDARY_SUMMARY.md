# Phase 5 Presentation Boundary Summary

## Purpose

Phase 5 establishes presentation boundaries across the product UI.

It does not fully clean every legacy UI island. A domain is Phase 5-ready when
portable presentation snapshots/models exist, legacy reads/writes are behind
source/sink adapters, at least one renderer or probe consumes the model, and
remaining legacy ownership is documented.

## Summary

| Domain | Boundary established | Adapter | Remaining legacy |
| --- | --- | --- | --- |
| Device | `DeviceStatusModel` | `LegacyAirDeviceStatusSource` | board/runtime status details |
| GPS | `GpsStatusModel` | `LegacyGpsStatusSource` | `gps_runtime` compatibility surface |
| Mesh | `MeshStatusModel` | `LegacyMeshStatusSource` | adapter-specific counters and runtime details |
| Settings | `SettingsModel` | `LegacySettingsSource` / `LegacySettingsActionSink` | full configuration migration |
| Chat | `ChatWorkspaceModel` | `ChatPresentationSource` / `LegacyChatActionSink` | `ChatUiController`, key verification, structured pending/failure |
| Team Chat | `ChatWorkspaceModel` with Team source/sink | `TeamChatPresentationSource` / `TeamChatActionSink` | location/command picker, rich payload UI, structured pending/failure |
| Map | `MapWorkspaceModel` | `LegacyMapPresentationSource` / `LegacyMapActionSink` | tile/cache/renderer/rich overlay cleanup |
| Workspace | `PresentationWorkspace` | target-owned composition | target construction order and legacy UI islands |

## Interpretation

Phase 5 means:

- renderers can consume portable presentation snapshots
- UI actions can flow through source/sink boundaries
- legacy ownership is named instead of hidden
- headless probes can validate the presentation graph

Phase 5 does not mean:

- Chat is fully clean
- Map is fully clean
- GPS compatibility reads are gone
- tile loading has moved
- all LVGL/uConsole surfaces have been rewritten
- Team rich payload rendering is finished

## Phase 5.8 Composition

Phase 5.8 adds:

- `PresentationWorkspace`
- `PresentationWorkspaceSnapshot`
- `PresentationWorkspaceProbe`
- linux_sim ASCII workspace probe
- uConsole presentation workspace smoke

These additions prove that Device, GPS, Mesh, Settings, Chat, Team Chat, and Map
can be represented as one typed presentation graph.

The workspace layer is intentionally not a factory, service locator, app service
registry, renderer, telemetry model, or snapshot cache.

## Phase 5.9 Renderer Hardening

Phase 5.9 starts locking renderer behavior:

- strict surfaces cannot include platform/toolkit/business runtime dependencies
- migrated Chat and Team text paths must continue using `ChatWorkspaceModel`
- migrated Map status/action paths must continue using `MapWorkspaceModel`
- LVGL dashboard GPS status labels now consume `GpsStatusModel`
- uConsole map status/center summary must continue exposing
  `presentation_workspace`
- ASCII/headless probes remain strict renderers that consume only
  `ui_presentation` models and fake source/sink implementations

uConsole tile/cache/contour enumeration, GTK overview GPS compatibility logic,
and dashboard/chat app model runtime reads remain bounded legacy ownership.
