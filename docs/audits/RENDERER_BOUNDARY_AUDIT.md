# Renderer Boundary Audit

## Purpose

Phase 5.9 hardens renderer boundaries.

It does not clean every legacy UI island. It prevents new renderer couplings
and locks migrated surfaces behind PresentationModel boundaries.

## Renderer Rule

Renderers may:

- consume presentation snapshots
- send presentation actions
- own widget/tree/layout/input state
- format pixels/text/layout

Renderers must not directly read:

- GPS runtime
- radio / mesh adapter
- `ChatService` / `ContactService`
- `TeamUiStore` raw state
- tile cache / SD map store
- Settings store
- board/device runtime
- protocol internals

## Renderer Categories

| Category | Examples | Phase 5.9 policy |
| --- | --- | --- |
| Clean renderer | ASCII probes, new snapshot renderers | strict |
| Migrated renderer surface | Chat non-team path, Team text path, Map workspace status | strict for migrated path |
| Legacy island | `ChatUiController`, `GPSPageState`, map viewport tile runtime | baseline |
| Platform application model | `UConsoleMapWorkspaceModel` | bounded legacy, not pure renderer |
| GTK/uConsole renderer | GTK app logic and widgets | strict only where a presentation bridge exists |

## Known Legacy Islands

- `ChatUiController`
- `GPSPageState`
- `ui/widgets/map/map_viewport`
- LVGL GPS/status page legacy sections
- LVGL dashboard GPS/compass/recent widgets
- uConsole tile/cache/contour model
- uConsole chat/dashboard app models
- uConsole GTK overview GPS compatibility logic
- key verification modal
- Team location/command picker
- route/tracker overlays
- rich Team map markers

## Strict Surfaces

These should not regress:

- `ui_presentation/*`
- `apps/linux_sim/tests/*ascii_probe*.cpp`
- `ChatPresentationSource` / `LegacyChatActionSink`
- `TeamChatPresentationSource` / `TeamChatActionSink`
- `LegacyMapPresentationSource` / `LegacyMapActionSink`
- `ChatWorkspaceModel`-backed non-team message rendering
- `TeamChatPresentationSource`-backed Team text rendering
- `MapWorkspaceModel`-backed map workspace status/action path
- LVGL dashboard GPS status labels backed by `GpsStatusModel`
- uConsole `presentation_workspace` bridge
- uConsole presentation workspace smoke

## Migrated Path Locks

Chat migrated path:

- non-team selection/read/send must continue flowing through `chat_model_`
- Team text projection/send must continue flowing through `team_chat_model_`
- key verification, event handling, conversation cache, and Team
  location/command picker remain legacy-owned

Map migrated path:

- compact LVGL map status/action path must continue using
  `map_workspace_model().snapshot()`
- compact LVGL center-on-self action must continue using
  `map_workspace_model().centerOnSelf()`
- uConsole map status/center summary must continue exposing
  `presentation_workspace`
- tile/cache/contour rendering remains legacy-owned

ASCII/headless migrated path:

- ASCII probes must consume `ui_presentation` models and fake source/sink
  implementations only
- ASCII probes must not directly include toolkit, runtime, store, or service
  headers
- `trailmate_map_workspace_ascii_probe` must render map workspace state from
  `MapWorkspaceModel`
- `trailmate_presentation_workspace_ascii_probe` must render Device/GPS/Mesh,
  Settings/Chat/TeamChat capability flags, and Map summary from
  `PresentationWorkspaceProbe`

LVGL dashboard migrated path:

- GPS status label, fix chip, and satellite count summary should continue using
  `dashboard_gps_status_model().snapshot()`
- the satellite radar remains a baseline legacy section until a richer GNSS
  skyplot presentation source exists

GTK/uConsole migrated path:

- `UConsoleMapWorkspaceModel` must keep exposing `presentation_workspace`
- `UConsoleMapWorkspaceModel` must keep map status/center projection behind
  `LegacyMapPresentationSource`, `LegacyMapActionSink`, and
  `MapWorkspaceModel`
- GTK overview GPS compatibility logic remains a baseline legacy island until a
  dedicated overview presentation model exists

## Deferred

- full `ChatUiController` cleanup
- full `GPSPageState` cleanup
- full map viewport tile/cache renderer cleanup
- full uConsole app model cleanup
- rich map overlay cleanup
- structured pending/failure
- rich Team payload rendering

## Interpretation

Phase 5.9 is a regression guard and incremental renderer hardening step.

Historical renderer couplings are recorded as a baseline. New renderer code must
prefer presentation models. New direct runtime reads must either move behind a
source/sink boundary or be added to the legacy baseline with an explicit reason.
