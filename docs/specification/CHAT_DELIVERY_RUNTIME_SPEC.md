# Chat Delivery Runtime Specification

## Purpose

Define structured runtime ownership for chat delivery, pending, and failure
state.

Phase 7.1 makes delivery state explicit without making the presentation model or
renderer responsible for pending queues.

## Core Rule

Delivery state is runtime state, not UI state.

```text
send result / ACK / failure event
  -> ChatDeliveryEventProjector
    -> ChatDeliveryReadModel
      -> ChatPresentationSource
        -> MessageRow delivery/failure
          -> Renderer
```

## Types

Phase 7.1 introduces:

- `ChatDeliveryRef`
- `DeliveryState`
- `DeliveryFailureKind`
- `ChatDeliveryRecord`
- `ChatDeliveryReadModel`
- `ChatDeliveryEventProjector`

Phase 7.3 adds:

- `ChatDeliveryEvent`
- `IChatDeliveryEventPort`
- `ProjectingChatDeliveryEventPort`
- `LegacyChatSendResultMapper`
- `LegacyChatDeliveryEventBridge`

## Ownership

`ChatDeliveryReadModel` owns UI-readable delivery records.

`ChatDeliveryEventProjector` updates the read model from send/delivery events.

`IChatDeliveryEventPort` is the runtime event sink port for delivery events.

`ProjectingChatDeliveryEventPort` adapts that port to
`ChatDeliveryEventProjector`.

`LegacyChatDeliveryEventBridge` maps existing `ChatSendResultEvent` and ACK
timeout hooks into the delivery event port.

`LegacyChatDeliveryBridge` maps existing coarse `ChatMessage::status` and
legacy send/failure concepts into delivery records.

`ChatPresentationSource` reads the delivery read model and projects state
into `MessageRow`.

## Boundaries

`ChatDeliveryReadModel` may:

- store bounded delivery records
- return records by `ChatDeliveryRef`
- clear records

`ChatDeliveryReadModel` must not:

- send messages
- retry messages
- inspect renderer state
- include LVGL/GTK
- include `ui_presentation`
- own radio or mesh adapters

`ChatDeliveryEventProjector` may:

- project queued/sending/sent/delivered/failed/received events
- map send failure kinds to delivery failure kinds

`ChatDeliveryEventProjector` must not:

- send packets
- retry messages
- build UI snapshots
- mutate ChatService storage directly
- render UI

`LegacyChatDeliveryEventBridge` may:

- consume legacy send result events
- look up the message needed to build `ChatDeliveryRef`
- publish delivery events through `IChatDeliveryEventPort`
- expose an ACK timeout projection hook

`LegacyChatDeliveryEventBridge` must not:

- send packets
- retry messages
- build UI snapshots
- include LVGL/GTK
- mutate renderer state

`ChatPresentationSource` may:

- read delivery records
- enrich `MessageRow.delivery`
- enrich `MessageRow.failure`

`ChatPresentationSource` must not:

- maintain pending queues
- receive send result events
- call the projector
- infer failure from renderer state

## Message Reference

Phase 7.1 uses `ChatDeliveryRef` as a compatibility reference:

- `local_id`
- `protocol_id`
- `nonce_or_seq`

Existing `ChatMessage::msg_id` maps to `protocol_id` first.

## Failure Kinds

Phase 7.1 recognizes:

- `PeerKeyMissing`
- `LocalIdentityMissing`
- `RadioSendFailed`
- `AckTimeout`
- `UnsupportedProtocol`
- `Rejected`
- `Unknown`

## Non-Goals

Phase 7.1 does not implement a full retry engine.

Phase 7.1 does not change radio packet format.

Phase 7.1 does not move all ChatService storage.

Phase 7.1 does not make `ChatWorkspaceModel` own delivery state.

Phase 7.1 does not make renderers infer pending/failure.

Phase 7.1 does not resolve Team rich payload delivery semantics.
