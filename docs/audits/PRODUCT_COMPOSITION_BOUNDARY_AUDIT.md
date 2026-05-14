# Product Composition Boundary Audit

## Purpose

Phase 6 establishes product composition and app shell boundaries.

It does not rewrite all legacy UI or runtime code. It identifies where object
graph construction currently lives and moves target-specific wiring toward
composition roots.

## Composition Rule

Composition roots wire object graphs.

They do not own business behavior, protocol interpretation, renderer widget
logic, GPS parsing, radio packet parsing, tile cache behavior, or message
policy.

## Current Composition Surfaces

| Surface | Current owner | Objects created | Problem | Target owner |
| --- | --- | --- | --- | --- |
| ESP Arduino app init | `apps/esp_pio` startup/runtime files | AppContext, LVGL runtime, GPS, mesh, BLE, UI entry points | target wiring mixed with runtime lifecycle and global AppContext | ESP LVGL composition root / legacy bridge |
| ESP-IDF app init | `apps/esp_idf` startup/runtime files | app facade runtime, GPS/radio/team adapters, loop runtime | target wiring mixed with platform boot and runtime scheduling | ESP-IDF composition root / legacy bridge |
| Linux simulator | `apps/linux_sim` CMake/tests/main | simulator shell, probes, fake presentation sources/sinks | mostly test composition, but not named as target composition | linux-sim composition root |
| uConsole GTK | `apps/linux_uconsole` / `platform/linux/uconsole` | GTK app state, uConsole chat/map/dashboard models | platform model, presentation graph, and renderer state are adjacent | linux-uconsole composition root |
| AppContext / facade access | shared app runtime and `app::messagingFacade()` | ChatService, ContactService, self node id, compatibility facades | implicit service locator | `LegacyAppContextBridge` |
| Chat page runtime | `modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp` | chat presentation sources/sinks and `ChatWorkspaceModel` objects | page runtime still owns part of presentation object graph | app shell composition root |
| ChatUiController | `modules/ui_shared/src/ui/screens/chat/chat_ui_controller.*` | consumes ChatService, chat models, event bus, LVGL state | controller is still composition-like legacy island | app shell / controller split |
| GPS page runtime | `modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp` | map presentation source/sink/model, map page runtime | renderer/runtime mixed with object graph bridge | app shell / map page bridge |
| Dashboard GPS widget | `modules/ui_shared/src/ui/menu/dashboard/dashboard_gps_widget.cpp` | local `GpsStatusModel` bridge | hardened slice, but still local construction | app shell / dashboard presentation binding |

## Object Graph Inventory

### App Services

- `ChatService`
- `ContactService`
- Team services / `TeamUiStore`
- Settings/config services
- mesh/radio services
- GPS services and compatibility runtime
- phone/BLE facades

### Presentation Sources/Sinks

- `LegacyAirDeviceStatusSource`
- `LegacyGpsStatusSource`
- `LegacyMeshStatusSource`
- `LegacySettingsSource`
- `LegacySettingsActionSink`
- `ChatPresentationSource`
- `LegacyChatActionSink`
- `TeamChatPresentationSource`
- `TeamChatActionSink`
- `LegacyMapPresentationSource`
- `LegacyMapActionSink`

### Presentation Models

- `DeviceStatusModel`
- `GpsStatusModel`
- `MeshStatusModel`
- `SettingsModel`
- `ChatWorkspaceModel`
- Team `ChatWorkspaceModel`
- `MapWorkspaceModel`
- `PresentationWorkspace`

### Renderers / Shells

- LVGL shell and page runtimes
- GTK/uConsole shell and app models
- ASCII/headless probes
- Linux simulator SDL shell

## Current Target-Specific Shape

### Linux Sim

Linux simulator already has small smoke/probe composition in tests. Phase 6
should name this as a `LinuxSimCompositionRoot` pilot so target object graph
ownership is explicit.

### Linux uConsole

uConsole already owns a GTK app state and platform-specific app models. Phase 6
should introduce a `UConsoleCompositionRoot` pilot that exposes
`PresentationBundle` without moving tile/cache/contour behavior into
presentation snapshots.

### ESP LVGL

ESP startup has sensitive static lifetime, task ordering, LVGL initialization,
GPS/radio/BLE initialization, and storage ordering constraints. Phase 6 should
introduce an ESP LVGL composition root bridge first, not reorder startup.

## Phase 6 Scope

Phase 6 will:

- introduce composition root contracts
- pilot Linux simulator composition
- pilot uConsole composition
- introduce an ESP LVGL composition bridge/audit
- contain AppContext behind explicit legacy bridge documentation
- add checker coverage for object graph construction boundaries

Phase 6 will not:

- remove all AppContext usage
- rewrite all renderers
- rewrite protocol/core services
- turn target manifests into runtime configuration
- fully reorder ESP startup
- move tile/cache into `MapWorkspaceSnapshot`

## Boundary Decision

Object graph construction should move toward target/app shell composition roots.

Existing page/controller/runtime construction is allowed only as bounded legacy
ownership. New presentation model/source/sink construction should prefer target
composition code or documented bridge objects.
