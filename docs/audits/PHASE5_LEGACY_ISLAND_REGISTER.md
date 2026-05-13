# Phase 5 Legacy Island Register

## Purpose

Phase 5 establishes presentation boundaries.

This register records UI legacy islands that remain after Phase 5.

These are not Phase 5 blockers because each is bounded, documented, and
protected from further uncontrolled expansion by audits, baselines, and
architecture checkers.

## Legacy Islands

### ChatUiController

Status: bounded legacy application controller.

Remaining ownership:

- LVGL state machine
- `ChatService` `processIncoming` / `flushStore`
- event bus handling
- key verification modal
- conversation cache
- contact fallback/title logic
- some Team compatibility flows

Boundary established:

- generic chat uses `ChatWorkspaceModel`
- non-team send/read/selection paths are guarded by renderer hardening checks
- Team text uses `TeamChatPresentationSource` / `TeamChatActionSink`

Future cleanup:

- `ChatApplicationController` split
- `ChatRenderer` split
- `KeyVerificationPresentation`
- structured pending/failure projection

### GPSPageState

Status: bounded legacy LVGL GPS/map page state.

Remaining ownership:

- some `gps_runtime` compatibility access
- page-local map state
- legacy display refresh
- LVGL widgets and timers
- map anchors and follow state

Boundary established:

- `GpsStatusModel`
- `MapWorkspaceModel`
- `LegacyMapPresentationSource`
- `LegacyMapActionSink`
- renderer hardening locks the migrated `map_workspace_model()` path

Future cleanup:

- GPS renderer split
- map page state split
- richer GNSS skyplot presentation source

### LVGL Dashboard GPS Widget

Status: partially hardened renderer.

Remaining ownership:

- per-satellite radar still reads legacy GNSS satellite snapshot

Boundary established:

- fix chip, fix label, and satellite count summary consume `GpsStatusModel`

Future cleanup:

- move per-satellite radar behind a GNSS skyplot presentation model

### Map Viewport / Tile Runtime

Status: bounded legacy renderer/runtime.

Remaining ownership:

- tile loading
- tile cache
- SD card tile lookup
- route/tracker overlay
- rich Team markers
- contour rendering
- gesture runtime and tile loader timers

Boundary established:

- `MapWorkspaceModel` handles workspace-level state only
- `MapWorkspaceSnapshot` intentionally does not contain tile/cache payloads

Future cleanup:

- `MapRenderEngine` boundary
- `TileSource` abstraction
- `OverlayPresentationSource`

### uConsole Map/Tile Model

Status: platform-specific legacy model.

Remaining ownership:

- tile/cache/contour enumeration
- platform-specific filesystem behavior
- pan/zoom persistence
- MQTT/contact node projection for map overlays

Boundary established:

- `presentation_workspace` bridge
- `MapWorkspaceModel` status path
- direct `platform/ui/gps_runtime.h` is not reintroduced into the uConsole map
  center/status path

Future cleanup:

- `UConsoleMapRenderer` split
- `TileCacheAdapter`
- `ContourPresentationSource`

### uConsole Chat/Dashboard/Overview Models

Status: platform-specific legacy application models.

Remaining ownership:

- `ChatService` / `ContactService` compatibility reads
- GPS compatibility reads in GTK overview/dashboard paths
- platform probe and cache details

Boundary established:

- uConsole can hold `PresentationWorkspace`
- uConsole chat/map presentation bridges are guarded by checker locks

Future cleanup:

- uConsole app model split
- overview/dashboard presentation models
- GTK renderer strictness expansion

### Team Location/Command Picker

Status: bounded Team legacy interaction.

Remaining ownership:

- location picker UI
- location marker send path
- command send UI
- rich payload rendering

Boundary established:

- Team text projection/send via Team source/sink
- Team rows use `ConversationKind::Team`
- Team is not mapped to DirectPeer or Channel

Future cleanup:

- `TeamRichPayloadPresentation`
- `TeamCommandActionSink`
- `TeamLocationActionSink`

### Structured Pending/Failure

Status: unresolved ownership.

Remaining ownership:

- failure detail is not preserved on `ChatMessage`
- ACK tracker is not projected into the chat read model
- pending queue ownership is not defined

Boundary established:

- coarse `Failed -> MessageFailureKind::Unknown` compatibility projection
- renderer/model are forbidden from inferring failure state

Future cleanup:

- `ChatPendingReadModel`
- Mesh-to-chat delivery projector
- structured delivery updates in `ChatService`

### Route / Tracker Overlays

Status: deferred renderer cleanup.

Remaining ownership:

- route storage reads
- route list rendering
- tracker overlay composition

Boundary established:

- route/tracker overlay code is recorded in renderer legacy baseline
- Map workspace state does not absorb route payloads

Future cleanup:

- Route presentation source
- tracker overlay presentation boundary

## Exit Interpretation

These islands do not block Phase 5 exit.

They are no longer uncontrolled violations. They are bounded legacy ownership
areas with explicit future cleanup paths. Phase 6 may build on the presentation
boundary while preserving these constraints.
