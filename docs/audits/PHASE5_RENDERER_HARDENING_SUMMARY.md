# Phase 5 Renderer Hardening Summary

## Purpose

Phase 5.9 hardens renderer boundaries.

It prevents new renderer couplings and locks migrated surfaces behind
PresentationModel boundaries.

This phase does not clean every legacy UI island.

## Strict Surfaces

| Surface | Rule |
| --- | --- |
| `ui_presentation/*` | no platform/toolkit/app service/runtime dependencies |
| ASCII/headless probes | no runtime/toolkit/store/service dependencies |
| Chat presentation source/sink | no renderer/toolkit/radio direct send dependencies |
| Team Chat presentation source/sink | no generic core chat conversion or legacy chat sink |
| Map presentation source/sink | no tile/cache/route/renderer dependencies |
| migrated Chat path | must use `ChatWorkspaceModel` |
| migrated Team text path | must use dedicated Team `ChatWorkspaceModel` |
| migrated Map status/action path | must use `MapWorkspaceModel` |
| LVGL dashboard GPS status | must use `GpsStatusModel` for status labels |
| uConsole presentation bridge | must expose `presentation_workspace` and workspace smoke |

## Legacy Islands

| Island | Status |
| --- | --- |
| `ChatUiController` | legacy application controller |
| `GPSPageState` | legacy LVGL map/GPS page state |
| `ui/widgets/map/map_viewport` | legacy tile renderer/runtime |
| LVGL dashboard satellite radar | legacy GNSS satellite detail renderer |
| uConsole tile/cache/contour | legacy platform model |
| uConsole chat/dashboard app models | legacy compatibility models |
| uConsole GTK overview GPS logic | legacy compatibility renderer |
| key verification modal | legacy |
| Team location/command picker | legacy |
| route/tracker overlays | legacy |

## Thin Slices Migrated or Locked

Phase 5.9 locks these migrated paths:

- non-team Chat selection, rendering, send, and read clearing through
  `chat_model_`
- Team text projection/send through `team_chat_model_`
- compact LVGL Map status/action path through `map_workspace_model()`
- uConsole Map presentation bridge through `presentation_workspace`
- ASCII map and workspace probes as strict headless renderers

Phase 5.9 also thins one LVGL status surface:

- LVGL dashboard GPS fix chip, fix label, and satellite count summary now use
  `GpsStatusModel`

The dashboard satellite radar remains legacy-owned because current portable GPS
status snapshots do not carry per-satellite skyplot details.

## Baseline

Known renderer legacy islands are recorded in:

```text
tools/architecture/phase5_renderer_legacy_baseline.json
```

The baseline exists to prevent noisy historical violations from hiding new
regressions.

## Policy

New renderer code must prefer presentation models.

New direct runtime reads must either:

1. move behind a source/sink boundary, or
2. be added to the renderer legacy baseline with an explicit reason and future
   cleanup owner.

Do not use Phase 5.9 as a reason to:

- rewrite `ChatUiController`
- rewrite `GPSPageState`
- move tile/cache state into `MapWorkspaceSnapshot`
- force rich Team payloads into generic rows
- remove all legacy platform application models at once
