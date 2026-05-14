# Phase 7 Runtime Ownership Register

## Purpose

Phase 7 records runtime ownership questions that must not be solved inside
presentation models or renderers.

This register starts with Chat delivery/pending/failure because that ownership
has direct user-visible consequences and a bounded migration path.

## Runtime Ownership Items

| Runtime item | Owner status | Phase | Notes |
| --- | --- | --- | --- |
| Chat delivery / pending / failure | contained | 7.1 / 7.6 | owned by `ChatDeliveryReadModel` and projector path; controller direct ownership is forbidden |
| Chat send-result projection | contained | 7.3 / 7.7 | `ChatSendResultEvent` projects through `LegacyChatDeliveryEventBridge`; `ChatPageRuntimeEventPump` owns forwarding |
| ACK timeout projection | explicitly deferred | 7.3 / final matrix | adapter hook projects `AckTimeout`; unified ACK source has exit condition in `PHASE7_FINAL_LEGACY_SURFACE_MATRIX.md` |
| key missing projection | explicitly deferred | 7.3 / final matrix | mapper supports structured failure; EventBus schema exit condition in `PHASE7_FINAL_LEGACY_SURFACE_MATRIX.md` |
| radio send failure projection | explicitly deferred | 7.3 / final matrix | mapper supports structured failure; EventBus schema exit condition in `PHASE7_FINAL_LEGACY_SURFACE_MATRIX.md` |
| Chat runtime event pump / store flush ownership | contained | 7.7 | owned by `ChatPageRuntimeEventPump` / `ChatPageRuntimeFacade`; controller is UI refresh sink |
| Chat retry/cancel/clear failure actions | contained | 7.4 / 7.6 | owned by `ChatDeliveryActionRequest` / `ChatDeliveryActionService`; controller direct action ownership is forbidden |
| Map tile/cache ownership | contained | 7.10 | tile path mapping and filesystem availability are owned by `MapTileResolver` / `LegacyFilesystemMapTileSource`; decoded image cache remains contained legacy |
| Map tile render queue / decoded cache ownership | contained | 7.11 | visible tile plan is projected into `MapTileRenderQueue`; ESP decoded image cache is wrapped by `LvglDecodedTileCache`; LVGL widget records remain contained legacy |
| Map overlay/route/tracker ownership | contained / explicitly deferred | 7.12 | current GPS and Team overlays project through `MapOverlaySnapshot` / `LegacyMapOverlaySource`; route/tracker overlay sources have exit conditions in final matrix |
| GPS page timers/tasks | contained | 7.13 | refresh cadence is owned by `GpsPageRuntimePump`; LVGL timers are tick hooks only |
| Team location/command action ownership | contained | 7.2 / 7.6 | owned by `TeamActionRequest` / `LegacyTeamActionBridge`; controller send payload encoding is forbidden |
| Team rich payload display ownership | contained | 7.8 | owned by `TeamRichPayloadProjector` / `TeamChatPresentationSource`; controller display decode/format is forbidden |
| Team position picker widget lifecycle | burned-down UI surface | 7.9 | owned by `TeamPositionPickerRenderer`; controller only handles selected/cancel workflow |
| key verification workflow | contained | 7.5 / 7.6 | owned by `KeyVerificationModel` plus legacy source/sink adapters; modal rendering is helper-bounded |

## Phase 7.1 Decision

Chat delivery state is runtime state.

It is not owned by:

- `ChatWorkspaceModel`
- renderer widgets
- `ChatUiController` local visual state
- `ui_presentation`

Phase 7.1 introduces:

- `ChatDeliveryReadModel`
- `ChatDeliveryEventProjector`
- `LegacyChatDeliveryBridge`
- optional delivery enrichment in `ChatPresentationSource`
- composition-root ownership in Linux simulator and uConsole pilots

## Remaining Legacy

Phase 7.1 does not fully clean:

- adapter ACK trackers
- radio send failure plumbing
- key verification modal
- ChatUiController event/state machine
- durable delivery storage
- retry/cancel commands

These are not blockers because the delivery ownership boundary is now explicit.

## Phase 7.3 Decision

Chat delivery events are runtime projection events.

They are not owned by:

- `ChatUiController`
- renderer widgets
- `ChatWorkspaceModel`
- `ui_presentation`

Phase 7.3 introduces:

- `IChatDeliveryEventPort`
- `ProjectingChatDeliveryEventPort`
- `LegacyChatSendResultMapper`
- `LegacyChatDeliveryEventBridge`

Existing `ChatSendResultEvent` projects coarse success/failure into
`ChatDeliveryReadModel`. Structured failure kinds are supported by mapper and
hook APIs, but the current EventBus schema does not yet carry those details.

## Phase 7.2 Decision

Team location and command sends are action/runtime ownership.

They are not owned by:

- `ChatWorkspaceModel`
- renderer widgets
- `ChatUiController` payload encoding
- `ui_presentation`

Phase 7.2 introduces:

- `TeamActionRequest`
- `ITeamActionSink`
- `ITeamLocationSource`
- `LegacyTeamActionBridge`
- Chat UI submission of location marker intent through the Team action sink

Team text remains on the existing `TeamChatActionSink` /
`team_chat_model_.sendMessage(...)` path. Rich Team payload rendering remains
deferred with an exit condition in the burn-down register.

## Phase 7.4 Decision

Chat delivery retry, cancel pending, and clear failure are runtime actions.

They are not owned by:

- `ChatWorkspaceModel`
- renderer widgets
- `ChatUiController` local state
- `ui_presentation`

Phase 7.4 introduces:

- `ChatDeliveryActionRequest`
- `IChatDeliveryActionSink`
- `IRetryChatMessagePort`
- `ChatDeliveryActionService`
- `LegacyChatDeliveryActionBridge`

`ClearFailure` removes failed records from `ChatDeliveryReadModel`.
`CancelPending` removes queued or sending projections only. `Retry` is a port
owned action and returns `Unsupported` until a retry port is wired.

## Phase 7.5 Decision

Key verification is an independent runtime/presentation workflow.

It is not owned by:

- `ChatWorkspaceModel`
- `MessageRow`
- renderer modal local state
- chat delivery read model
- `ChatUiController` workflow fields

Phase 7.5 introduces:

- `KeyVerificationSnapshot`
- `IKeyVerificationPresentationSource`
- `IKeyVerificationActionSink`
- `KeyVerificationModel`
- `LegacyKeyVerificationSource`
- `LegacyKeyVerificationActionSink`

The LVGL modal may remain in `ChatUiController`, but it must render snapshots
and submit actions through the model. MeshCore/Meshtastic differences remain
behind legacy key verification adapters.

## Phase 7.6 Decision

Phase 7.6 is a burn-down pass, not a new model phase.

Legacy surfaces covered by Phase 7 owners are tracked in
`LEGACY_BURNDOWN_REGISTER.md`. `ChatUiController` responsibilities are
classified in `CHAT_UI_CONTROLLER_BURNDOWN_AUDIT.md`.

Phase 7.6 burned down:

- direct Team location/command send payload ownership in `ChatUiController`
- direct key verification runtime API ownership in `ChatUiController`
- direct delivery read/action ownership in `ChatUiController`
- key verification modal rendering inside the controller body

Remaining legacy surfaces now require removal conditions. Subsequent burn-down phases
may move those surfaces from contained to burned-down:

- `LegacyChatDeliveryEventBridge`
- `LegacyChatDeliveryActionBridge`
- `LegacyTeamActionBridge`
- `LegacyKeyVerificationSource`
- `LegacyKeyVerificationActionSink`
- controller-owned `ChatService::processIncoming` / `flushStore` was removed in Phase 7.7

## Phase 7.7 Decision

Chat runtime event projection and ChatService scheduling are runtime pump
responsibilities.

They are not owned by:

- `ChatUiController`
- renderer widgets
- `ChatWorkspaceModel`
- `ui_presentation`

Phase 7.7 introduces:

- `IChatUiRefreshSink`
- `ChatPageRuntimeEventPump`
- `ChatPageRuntimeFacade`

`ChatPageRuntimeFacade` remains compatible with the existing
`IChatUiRuntime` app facade hook. It calls the event pump first and then lets
`ChatUiController` refresh UI state.

`ChatUiController` no longer:

- calls `ChatService::processIncoming()`
- calls `ChatService::flushStore()`
- receives EventBus events directly
- calls `LegacyChatDeliveryEventBridge`
- updates `LegacyKeyVerificationSource`

## Phase 7.8 Decision

Team location and command display are presentation projection concerns.

They are not owned by:

- `ChatUiController`
- renderer local state
- `ChatWorkspaceModel`
- Team action send sinks

Phase 7.8 introduces:

- `TeamRichPayloadDisplay`
- `TeamRichPayloadProjector`
- `TeamChatPresentationSource` consumption of projected rich payload summaries

`ChatUiController` no longer:

- defines `format_team_chat_entry(...)`
- calls `decodeTeamChatLocation(...)`
- calls `decodeTeamChatCommand(...)`
- constructs `TeamChatLocation` / `TeamChatCommand` for display formatting

The current renderer still consumes summary text through `MessageRow`. Rich Team
cards and additional Map overlay visuals are deferred with exit conditions.

## Phase 7.9 Decision

Team position picker lifecycle is a widget renderer concern.

It is not owned by:

- Team action send sinks
- Team protocol payload encoders
- GPS runtime
- `ChatWorkspaceModel`
- `ChatUiController` LVGL member refs

Phase 7.9 introduces:

- `TeamPositionPickerRenderer`
- `TeamPositionPickerRenderer::Callbacks`

`ChatUiController` no longer stores picker overlay, panel, description label,
LVGL group, previous group, or icon event contexts. It no longer defines picker
LVGL event callbacks or directly creates the picker widget tree.

The controller keeps the selection workflow. `sendTeamLocationWithIcon(...)`
remains only because it submits `TeamActionRequest` through `ITeamActionSink`;
it must not encode Team payloads directly.

## Phase 7.12 Decision

Map overlay semantic state is presentation projection state.

It is not owned by:

- tile source/cache
- renderer widgets
- GPS runtime source
- Team store
- route/tracker stores

Phase 7.12 introduces:

- `MapOverlaySnapshot`
- `IMapOverlayPresentationSource`
- `MapOverlayProjector`
- `LegacyMapOverlaySource`

Current GPS and Team latest location overlays are projected through the overlay
source boundary. Route/tracker and measurement overlays are explicitly deferred
with removal conditions in `PHASE7_FINAL_LEGACY_SURFACE_MATRIX.md`.

## Phase 7.13 Decision

GPS page refresh cadence is runtime scheduling state.

It is not owned by:

- GPS renderers
- map widgets
- page widget local state
- GPS status snapshots

Phase 7.13 introduces:

- `IGpsUiRefreshSink`
- `IGpsStatusRefreshModel`
- `GpsPageRuntimePump`

LVGL timers may remain as platform tick hooks during migration, but the refresh
interval and active/inactive lifecycle belong to `GpsPageRuntimePump`.
