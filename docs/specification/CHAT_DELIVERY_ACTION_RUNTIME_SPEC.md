# Chat Delivery Action Runtime Specification

## Purpose

Define runtime/action ownership for chat delivery actions:

- retry
- cancel pending
- clear failure

These actions are commands against delivery runtime state. They are not
presentation model state and they are not renderer behavior.

## Core Rule

Delivery actions belong to runtime/action sinks.

```text
renderer/controller intent
  -> ChatDeliveryActionRequest
    -> IChatDeliveryActionSink
      -> ChatDeliveryActionService
        -> ChatDeliveryReadModel / retry port
```

`ChatWorkspaceModel` must not own retry, cancel, or clear-failure state.

## Types

Phase 7.4 defines:

- `ChatDeliveryActionKind`
- `ChatDeliveryActionRequest`
- `ChatDeliveryActionFailure`
- `ChatDeliveryActionResult`
- `IChatDeliveryActionSink`
- `IRetryChatMessagePort`
- `ChatDeliveryActionService`
- `LegacyChatDeliveryActionBridge`

## Ownership

`ChatDeliveryActionRequest` is a command DTO. It describes the requested
delivery action and carries a `ChatDeliveryRef`.

`IChatDeliveryActionSink` handles delivery actions. It must not build snapshots
or render UI.

`ChatDeliveryActionService` is the first delivery action command handler. It
may clear records from `ChatDeliveryReadModel` or delegate retry to
`IRetryChatMessagePort`.

`IRetryChatMessagePort` is a future retry delegation point. It is intentionally
separate from the read model so Phase 7.4 does not imply a complete retry
engine.

`LegacyChatDeliveryActionBridge` maps compatibility UI message references into
delivery action requests. It must not mutate the read model directly.

## ClearFailure

`ClearFailure` behavior:

- invalid reference returns `InvalidRef`
- missing record returns `NotFound`
- failed record is removed from `ChatDeliveryReadModel`
- non-failed record returns `NotRetryable`

This action clears the projected failure state. It does not delete the chat
message.

## CancelPending

`CancelPending` behavior:

- invalid reference returns `InvalidRef`
- missing record returns `NotFound`
- queued or sending record is removed from `ChatDeliveryReadModel`
- sent, delivered, received, failed, or unknown records return `NotRetryable`

Phase 7.4 only clears the projection. It does not cancel an already submitted
radio packet.

## Retry

`Retry` behavior:

- invalid reference returns `InvalidRef`
- if no `IRetryChatMessagePort` is wired, returns `Unsupported`
- if a retry port is wired, delegates to `retryMessage(ref)`

The retry port owns resend semantics. `ChatDeliveryActionService` does not
resend packets itself.

## Boundaries

`ChatDeliveryActionService` must not:

- include LVGL or GTK
- include `ui_presentation`
- include `ChatWorkspaceModel`
- build `MessageRow`
- render UI
- own packet send policy

`LegacyChatDeliveryActionBridge` must not:

- include LVGL or GTK
- build `ChatWorkspaceSnapshot`
- access `ChatWorkspaceModel`
- directly clear `ChatDeliveryReadModel`
- retry or send messages itself

`ChatUiController` may submit a delivery action through a bridge in a future UI
entry point. It must not own the action service or mutate the read model.

## Non-Goals

Phase 7.4 does not implement a full retry engine.

Phase 7.4 does not add retry button or menu UX.

Phase 7.4 does not cancel radio packets.

Phase 7.4 does not delete failed chat messages.

Phase 7.4 does not make `ChatWorkspaceModel` own delivery actions.
