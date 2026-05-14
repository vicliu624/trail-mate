# Phase 5 Presentation Conformance Matrix

## Purpose

This matrix records the Phase 5 presentation boundary status.

A domain is Phase 5-ready when it has:

- portable snapshot/model
- source/sink boundary where actions exist
- tests
- at least one renderer/probe consumer
- documented legacy ownership
- checker coverage

Phase 5-ready does not mean fully clean.

## Matrix

| Domain | Model | Snapshot | Source | Sink | Tests | Consumer | Legacy audit | Status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Device | `DeviceStatusModel` | `DeviceStatusSnapshot` | `IDeviceStatusSource` / `LegacyAirDeviceStatusSource` | read-only | `test_device_status_model.cpp` | status/probe consumers | known violations | ready |
| GPS | `GpsStatusModel` | `GpsStatusSnapshot` | `IGpsStatusSource` / `LegacyGpsStatusSource` | read-only | `test_gps_status_model.cpp` | dashboard status, map source, probes | `GPS_CONSUMER_BOUNDARY_AUDIT.md` | ready-with-legacy |
| Mesh | `MeshStatusModel` | `MeshStatusSnapshot` | `IMeshStatusSource` / `LegacyMeshStatusSource` | read-only | `test_mesh_status_model.cpp` | status/probe consumers | known violations | ready-with-legacy |
| Settings | `SettingsModel` | `SettingsSnapshot` | `ISettingsSource` / `LegacySettingsSource` | `ISettingsActionSink` / `LegacySettingsActionSink` | `test_settings_model.cpp` | settings surface | known violations | ready-with-legacy |
| Chat | `ChatWorkspaceModel` | `ChatWorkspaceSnapshot` | `ChatPresentationSource` | `LegacyChatActionSink` | chat workspace and source/sink tests | `ChatUiController` migrated path | `CHAT_UI_CONTROLLER_LEGACY_OWNERSHIP_AUDIT.md` | ready-with-legacy |
| Team Chat | Team `ChatWorkspaceModel` | `ChatWorkspaceSnapshot` | `TeamChatPresentationSource` | `TeamChatActionSink` | Team chat source/sink tests | `ChatUiController` Team text path | `TEAM_CHAT_PRESENTATION_AUDIT.md` | ready-with-legacy |
| Map | `MapWorkspaceModel` | `MapWorkspaceSnapshot` | `LegacyMapPresentationSource` | `LegacyMapActionSink` | map workspace and legacy adapter tests | LVGL, uConsole, ASCII probe | `MAP_UI_LEGACY_OWNERSHIP_AUDIT.md` | ready-with-legacy |
| Workspace | `PresentationWorkspace` | `PresentationWorkspaceSnapshot` | n/a | n/a | workspace/probe tests | ASCII workspace probe, uConsole smoke | `PHASE5_PRESENTATION_BOUNDARY_SUMMARY.md` | ready |
| Renderer | n/a | presentation snapshots | n/a | presentation actions | renderer checker/probe tests | LVGL, GTK/uConsole, ASCII/headless | `PHASE5_RENDERER_HARDENING_SUMMARY.md` | ready-with-legacy |

## Status Definitions

### ready

The presentation boundary is established and legacy ownership is minimal or not
relevant to the domain.

### ready-with-legacy

The presentation boundary is established, but legacy islands remain documented,
bounded, and covered by regression checks or baseline records.

### blocked

The presentation boundary is missing or unsafe.

## Checker Coverage

Phase 5 readiness is covered by:

- `tools/architecture/check_phase5_ready.py`
- `tools/architecture/check_phase5_workspace_ready.py`
- `tools/architecture/check_phase5_renderer_ready.py`

The final readiness gate aggregates these checks in
`tools/architecture/check_phase5_final_ready.py`.

## Phase 5 Exit Interpretation

Phase 5 exits successfully if all domains are either:

- ready
- ready-with-legacy

No domain may remain blocked.

Chat, Team Chat, Map, Renderer, GPS, Mesh, and Settings are not declared clean.
They are declared bounded enough for Phase 6 to build on the presentation
boundary without reintroducing uncontrolled renderer/runtime coupling.
