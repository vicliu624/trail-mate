# Chat Delivery Ownership Audit

## Purpose

Phase 7.1 establishes runtime ownership for chat delivery, pending, and failure
state.

This audit does not implement a complete reliable messaging system. It records
where delivery state currently comes from, where failure detail is lost, and
which runtime/read-model boundary should own UI-readable delivery state.

## Current State

| State | Current source | Current reader | Problem |
| --- | --- | --- | --- |
| Queued | `ChatMessage::status` | `ChatPresentationSource` / chat mapper | coarse only |
| Sent | `ChatMessage::status` | `ChatPresentationSource` / chat mapper | send success is not delivery acknowledgement |
| Failed | `ChatMessage::status` | `ChatPresentationSource` / chat mapper | failure reason is lost |
| Received | `ChatMessage::status` | `ChatPresentationSource` / chat mapper | incoming state is readable but not part of a delivery model |
| PeerKeyMissing | send result / mesh protocol path | not projected consistently | UI sees `Unknown` failure |
| LocalIdentityMissing | send result / mesh protocol path | not projected consistently | UI sees `Unknown` failure |
| RadioSendFailed | radio/send path | not projected consistently | UI sees `Unknown` failure |
| UnsupportedProtocol | action/send path | action result only | not tied to message delivery record |
| Rejected | action/send path | action result only | not tied to message delivery record |
| AckTimeout | ACK tracker / retry path | not unified | no stable owner |

## Current Send Result

Current send result is split across:

- `ChatService::sendText(...)`, which creates a `ChatMessage` with coarse
  `MessageStatus::Queued` or `MessageStatus::Failed`
- mesh adapter send return values
- protocol-specific direct send paths that may know peer key or identity
  failures
- UI action sinks that expose only coarse `UiActionResult`

The presentation layer can tell that a message failed, but cannot reliably tell
why.

## Current ChatMessage Status

`ChatMessage::status` can express:

- `Incoming`
- `Queued`
- `Sent`
- `Failed`

It cannot express:

- sending
- delivered/ACKed
- peer key missing
- local identity missing
- radio send failure
- ACK timeout
- unsupported protocol
- rejected by runtime policy

## Current ACK Timeout

ACK timeout and retry state are currently adapter/runtime concerns. They are not
projected into a stable UI-readable chat delivery model.

Phase 7.1 should not move ACK behavior itself. It should create a place where
ACK timeout events can be projected.

## Current Failure Visibility

The UI currently learns failure through coarse message status or action result.

Renderer code must not infer failure type from:

- labels
- colors
- button state
- spinner state
- transient controller flags
- ChatUiController local widget state

## Transient vs Persistent State

| State | Phase 7.1 interpretation |
| --- | --- |
| queued | UI-readable runtime projection |
| sending | transient runtime projection |
| sent | runtime/store projection |
| delivered | future ACK projection |
| failed | runtime projection with structured reason |
| key missing | structured failure reason |
| radio failed | structured failure reason |
| ACK timeout | structured failure reason from future ACK owner |

Phase 7.1 does not require every delivery record to become durable storage.
It does require a stable owner for the UI-readable projection.

## Boundary Rule

`ChatWorkspaceModel` must not own pending/failure.

Renderer must not infer delivery/failure from labels, colors, or button state.

`ChatPresentationSource` may read a delivery read model and enrich
`MessageRow`, but it must not maintain pending queues or receive send events.

## Target Ownership

Delivery runtime state is owned by:

```text
ChatDeliveryReadModel / ChatDeliveryStateStore
```

or by `ChatService` only if `ChatService` later explicitly stores structured
delivery state.

## Phase 7.1 Decision

Phase 7.1 introduces a dedicated delivery read model first.

Later phases may merge or persist this state through `ChatService` if that
becomes the right runtime owner. Until then, delivery state remains outside:

- `ChatWorkspaceModel`
- renderer widgets
- ChatUiController visual state
- `ui_presentation`

## Non-Blockers

The following remain future work:

- full retry engine
- durable ACK state
- radio adapter cleanup
- key verification flow cleanup
- ChatUiController split
- Team rich payload delivery state
