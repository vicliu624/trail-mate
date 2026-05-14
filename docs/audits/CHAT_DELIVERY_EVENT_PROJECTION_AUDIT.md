# Chat Delivery Event Projection Audit

## Purpose

Phase 7.3 connects runtime send/delivery events to
`ChatDeliveryEventProjector`.

It does not implement a full ACK/retry system. It projects currently available
send-result facts into `ChatDeliveryReadModel` and defines hooks for delivery
events that do not yet have a unified source.

## Current Event Sources

| Event | Current source | Data available | Projection status |
| --- | --- | --- | --- |
| `ChatSendResultEvent` | EventBus / radio adapter / app runtime path | `msg_id`, `success`, event timestamp | can project `Sent` / `Failed` |
| `PeerKeyMissing` | mesh/direct send result | available in `mesh::SendFailure`, not in `ChatSendResultEvent` | mapper/hook defined |
| `LocalIdentityMissing` | mesh/direct send result | available in `mesh::SendFailure`, not in `ChatSendResultEvent` | mapper/hook defined |
| `RadioSendFailed` | mesh/direct send result | available in `mesh::SendFailure`, not in `ChatSendResultEvent` | mapper/hook defined |
| `UnsupportedProtocol` | mesh/direct send result | available in legacy send failure paths, not in `ChatSendResultEvent` | mapper/hook defined |
| `Rejected` | UI/action sink or runtime coarse failure | coarse only unless adapter passes detail | mapper/hook defined |
| `AckTimeout` | ACK tracker / send-result timeout path | not unified | adapter hook defined first |

## Current `ChatSendResultEvent`

`ChatSendResultEvent` currently carries:

- `msg_id`
- `success`
- event timestamp inherited from `sys::Event`

It does not carry structured failure detail. Phase 7.3 therefore maps
`success=false` to:

```text
DeliveryState::Failed + SendFailureKind::Unknown
```

until a future event can provide a structured failure kind.

## Current ACK Timeout

ACK timeout ownership is not unified. Some paths treat send-result failure as a
coarse timeout/failure, while lower-level mesh send code can expose
`AckTimeout`.

Phase 7.3 introduces an adapter hook:

```text
LegacyChatDeliveryEventBridge::onAckTimeout(...)
```

This hook projects:

```text
DeliveryState::Failed + SendFailureKind::AckTimeout
```

It does not retry, cancel, or change message storage.

## Rule

`ChatUiController` must not update delivery read model.

Event projection must be owned by composition root / runtime bridge. A legacy
controller may forward an existing event to the bridge while the bridge and
read model remain owned by runtime/composition code.

## Phase 7.3 Decision

Phase 7.3 introduces:

- `IChatDeliveryEventPort`
- `ProjectingChatDeliveryEventPort`
- `LegacyChatSendResultMapper`
- `LegacyChatDeliveryEventBridge`

`ChatPresentationSource` continues to read `ChatDeliveryReadModel` and
enrich `MessageRow.delivery` / `MessageRow.failure`.

## Deferred

- `ChatSendResultEventV2` with structured failure kind
- direct mesh send-result detail plumbing into EventBus
- ACK tracker unification
- retry/cancel actions
- durable delivery state storage
