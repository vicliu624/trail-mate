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
| ACK timeout projection | future | 7.x | events can project into delivery read model |
| key missing projection | in progress | 7.1 | structured failure kind exists |
| radio send failure projection | in progress | 7.1 | structured failure kind exists |
| Chat retry/cancel actions | future | 7.x | not implemented in 7.1 |
| Map tile/cache ownership | future | later phase | must not move into `MapWorkspaceSnapshot` |
| Team location/command delivery | future | later phase | rich payload semantics remain separate |
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
