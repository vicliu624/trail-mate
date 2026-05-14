# Phase 7 Runtime Ownership Register

## Purpose

Phase 7 records runtime ownership questions that must not be solved inside
presentation models or renderers.

This register starts with Chat delivery/pending/failure because that ownership
has direct user-visible consequences and a bounded migration path.

## Runtime Ownership Items

| Runtime item | Owner status | Phase | Notes |
| --- | --- | --- | --- |
| Chat delivery / pending / failure | in progress | 7.1 | owned by `ChatDeliveryReadModel` and projector path |
| Chat send-result projection | in progress | 7.3 | `ChatSendResultEvent` projects through `LegacyChatDeliveryEventBridge` |
| ACK timeout projection | in progress | 7.3 | adapter hook projects `AckTimeout`; unified ACK source remains future |
| key missing projection | in progress | 7.3 | mapper supports structured failure; EventBus schema still coarse |
| radio send failure projection | in progress | 7.3 | mapper supports structured failure; EventBus schema still coarse |
| Chat retry/cancel actions | future | 7.x | not implemented in 7.1 |
| Map tile/cache ownership | future | later phase | must not move into `MapWorkspaceSnapshot` |
| Team location/command action ownership | in progress | 7.2 | owned by `TeamActionRequest` / `LegacyTeamActionBridge`; rich rendering remains future |
| key verification UI | future | later phase | not delivery read model ownership |
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
