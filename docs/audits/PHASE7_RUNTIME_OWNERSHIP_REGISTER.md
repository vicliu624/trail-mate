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
| Chat send-result projection | contained | 7.3 / 7.6 | `ChatSendResultEvent` projects through `LegacyChatDeliveryEventBridge`; event pump extraction remains future |
| ACK timeout projection | in progress | 7.3 | adapter hook projects `AckTimeout`; unified ACK source remains future |
| key missing projection | in progress | 7.3 | mapper supports structured failure; EventBus schema still coarse |
| radio send failure projection | in progress | 7.3 | mapper supports structured failure; EventBus schema still coarse |
| Chat runtime event pump / store flush ownership | contained | 7.7 | owned by `ChatPageRuntimeEventPump` / `ChatPageRuntimeFacade`; controller is UI refresh sink |
| Chat retry/cancel/clear failure actions | contained | 7.4 / 7.6 | owned by `ChatDeliveryActionRequest` / `ChatDeliveryActionService`; controller direct action ownership is forbidden |
| Map tile/cache ownership | future | later phase | must not move into `MapWorkspaceSnapshot` |
| Team location/command action ownership | contained | 7.2 / 7.6 | owned by `TeamActionRequest` / `LegacyTeamActionBridge`; controller send payload encoding is forbidden |
| key verification workflow | contained | 7.5 / 7.6 | owned by `KeyVerificationModel` plus legacy source/sink adapters; modal rendering is helper-bounded |
| GPS page timers/tasks | future | later phase | runtime scheduling owner still legacy |

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
- optional delivery enrichment in `LegacyChatPresentationSource`
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
future work.

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

Remaining legacy surfaces now require removal conditions:

- `LegacyChatDeliveryEventBridge`
- `LegacyChatDeliveryActionBridge`
- `LegacyTeamActionBridge`
- `LegacyKeyVerificationSource`
- `LegacyKeyVerificationActionSink`
- Team rich payload formatting in `ChatUiController`
- controller-owned `ChatService::processIncoming` / `flushStore`

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
