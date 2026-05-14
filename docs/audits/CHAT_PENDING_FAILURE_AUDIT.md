# Chat Pending Failure Audit

Phase 5.6-e exposes the first chat delivery projection, but the current
business model does not yet preserve structured send failure reasons all the
way to `ChatWorkspaceSnapshot`.

## Boundary

`ChatWorkspaceModel` does not own pending messages, retry state, ACK tracking,
or failure inference. It only forwards UI actions and asks the presentation
source for an immutable snapshot.

Renderer code must not infer pending or failure state from colors, labels,
last send result events, or local widget state. Delivery and failure state must
come from:

```text
ChatService / MeshSession / ACK tracker / pending store
    -> ChatPresentationSource
        -> ui::chat::MessageRow.delivery
        -> ui::chat::MessageRow.failure
```

## Current Projection

The current `chat_presentation_adapters::mapMessageStatus` mapping is:

| Source state | UI delivery state | UI failure kind | Status |
| --- | --- | --- | --- |
| `chat::MessageStatus::Incoming` | `Received` | `None` | Supported |
| `chat::MessageStatus::Queued` | `Queued` | `None` | Supported |
| `chat::MessageStatus::Sent` | `Sent` | `None` | Supported |
| `chat::MessageStatus::Failed` | `Failed` | `Unknown` | Supported as coarse fallback |

`chat::ChatMessage` currently carries only coarse `MessageStatus`. It does not
carry the reason a send failed.

## Structured Failure Sources

| Failure | Known current source | Persisted in `ChatMessage` | UI projection today | Gap |
| --- | --- | --- | --- | --- |
| `PeerKeyMissing` | `mesh::SendFailure::PeerKeyMissing` / mesh event | No | `Unknown` after coarse failure | Failure reason is not stored with chat message |
| `LocalIdentityMissing` | `mesh::SendFailure::LocalIdentityMissing` | No | `Unknown` after coarse failure | Failure reason is not stored with chat message |
| `RadioSendFailed` | `mesh::SendFailure::RadioSendFailed` | No | `Unknown` after coarse failure | Failure reason is not stored with chat message |
| `AckTimeout` | `mesh::SendFailure::AckTimeout` or legacy ACK timeout | No | `Unknown` after coarse failure | ACK ownership is split across adapters |
| `UnsupportedProtocol` | `mesh::SendFailure::ProtocolUnsupported` or UI sink unsupported route | No | `Unsupported` action result or `Unknown` message failure | Action failure and stored message failure are separate |
| `Rejected` | `ChatService::sendText` returning no message id or failed stored message | No structured detail | `Rejected` action result, `Unknown` message failure | Synchronous action result is not persisted as message failure detail |

## Ownership Decision

Until a stable owner is chosen, Phase 5.6-e must not add a pending queue to
`ChatWorkspaceModel` and must not make renderer code remember send failures.

The next viable owner should be one of:

- `ChatService`, if message delivery state is persisted as part of chat history.
- A chat pending/failure read model, if ACK and retry state remains separate
  from stored history.
- A Mesh-to-chat event projection, if structured `mesh::SendResult` events are
  normalized before reaching chat presentation.

Whichever owner is chosen must expose enough state for
`IChatPresentationSource` to fill:

```text
MessageRow.delivery
MessageRow.failure
MessageRef.local_id or MessageRef.nonce_or_seq
```

## Current Constraint

The chat read projection may continue projecting:

```text
Failed -> MessageDeliveryState::Failed
Failed -> MessageFailureKind::Unknown
```

That fallback is acceptable only while structured failure ownership is missing.
It must not be interpreted as completion of detailed pending/failure UI.

## Phase 5.6 Closeout Decision

Phase 5.6-closeout does not introduce a new pending queue.

Structured failure remains deferred until one of the following is chosen:

1. `ChatService` stores structured failure on `ChatMessage`.
2. A ChatPendingReadModel aggregates `MeshSession` / ACK tracker / send result
   events.
3. A Mesh-to-chat event projector normalizes `SendResult` into chat delivery
   updates.

Until then:

```text
Failed -> MessageDeliveryState::Failed
Failed -> MessageFailureKind::Unknown
```

is the only supported compatibility projection.

## Prohibited Workaround

Do not:

- keep last send failure in `ChatWorkspaceModel`
- let LVGL widgets remember failure state
- infer failure from text color
- infer pending from a button state
- create a renderer-side pending message list
