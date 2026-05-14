# Chat Delivery Action Ownership Audit

## Purpose

Phase 7.4 establishes ownership for chat delivery actions:

- retry
- cancel pending
- clear failure

These actions are runtime commands. They are not presentation model state and
not renderer state.

## Current State

| Action | Current owner | Current UI path | Problem |
| --- | --- | --- | --- |
| Retry failed message | `ChatService::resendFailed` exists, but no delivery action owner | not consistently exposed | retry ownership is not explicit |
| Cancel queued/sending | no unified pending owner | no stable UI path | cancelling radio/runtime send is not modeled |
| Clear failure | `ChatDeliveryReadModel` can clear records, but callers are not owned | tests/manual enrichment only | renderer/controller could be tempted to mutate read model |

## Current Failed Message Retry

`ChatService` can resend failed messages by message id, but this is not the same
as a delivery action boundary.

Phase 7.4 defines a retry port first:

```text
ChatDeliveryActionService
  -> IRetryChatMessagePort
```

The concrete retry engine remains deferred.

## Current Pending Cancel

There is no unified cancel mechanism for already queued radio/runtime sends.

Phase 7.4 only defines projection ownership:

```text
CancelPending
  -> clear queued/sending delivery projection
```

It does not cancel an already submitted radio packet.

## Current Clear Failure

`ChatDeliveryReadModel::clear(...)` can remove a delivery record. Phase 7.4
makes `ChatDeliveryActionService` the owner of this command path.

Renderers and controllers must not directly clear delivery records.

## Boundary Rule

Delivery actions belong to:

```text
ChatDeliveryActionRequest
  -> IChatDeliveryActionSink
    -> ChatDeliveryActionService
```

They do not belong to:

- `ChatWorkspaceModel`
- renderer widgets
- `ChatUiController` local state
- `ui_presentation`

## Phase 7.4 Decision

Phase 7.4 implements:

- `ClearFailure` for failed delivery records
- `CancelPending` for queued/sending delivery records
- `Retry` as a port-owned action, returning `Unsupported` when no retry port is
  wired

Phase 7.4 adds `LegacyChatDeliveryActionBridge` for UI/runtime compatibility.
The bridge maps `MessageRef` to `ChatDeliveryRef` and calls the delivery action
sink. It must not render UI, build snapshots, or mutate the read model directly.

## Deferred

- full retry engine
- durable pending queue
- radio packet cancellation
- retry button/menu UX
- structured retry failure reporting
