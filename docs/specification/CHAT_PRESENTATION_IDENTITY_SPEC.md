# Chat Presentation Identity Spec

Phase 5.6-pre freezes the identity contract used by chat presentation snapshots.
It does not introduce a new chat business model and does not change
`core_chat`.

## Boundary

`chat::ConversationId` is the business conversation identity. It belongs to
`core_chat` and expresses protocol, channel, and peer.

`ui::chat::ConversationId` is a presentation selection token. It belongs to
`ui_presentation` and expresses which row or conversation view the UI has
selected.

Those two identities are related by an adapter mapper. `ui_presentation` must
not include `chat/domain/chat_types.h`, `ChatService`, `ContactService`,
`IMeshAdapter`, store cursors, or platform/runtime headers.

## Direction

```text
core_chat types
    -> ChatPresentationSource / mapper adapter
        -> ui::chat snapshot types
            -> LVGL / ASCII / GTK renderer
```

The adapter is the only layer allowed to include both core chat identity types
and UI presentation identity types.

## Rules

- `chat::ConversationId` remains the source of truth for core chat sessions.
- `ui::chat::ConversationId` is only a UI selection token.
- `ChatWorkspaceRequest` carries UI selection and list offsets into the
  presentation source. Its offsets are UI window offsets, not database cursors
  or `ChatService` cursors.
- `ChatWorkspaceModel` may own only UI selection state and cursor offsets. It
  must delegate snapshot construction to `IChatPresentationSource` and user
  actions to `IChatActionSink`.
- `ChatWorkspaceModel::selectConversation` uses optimistic UI selection:
  it validates the presentation id, updates local selected state immediately,
  resets the message offset, and then notifies `IChatActionSink`. A sink
  failure does not roll back the UI selection because selection is presentation
  state, while the sink is a synchronization hook for app-side side effects
  such as read cursors and mark-read behavior.
- `ChatWorkspaceModel` must not own or expose `ChatService`, `ContactService`,
  `IMeshAdapter`, stores, protocol adapters, or `chat::ConversationId`.
- `ChatWorkspaceSnapshot` must not expose `ChatService`, `ContactService`,
  `IMeshAdapter`, store cursors, or `chat::ConversationId`.
- `SendMessageView` uses `ui::chat::ConversationId`, not a bare peer node id.
- `MessageRow` uses `ui::chat::MessageRef`; it must not assume a message id is
  a database row id, packet id, retry token, or pending id.
- Team, system, broadcast, and diagnostic presentation rows do not have to be
  real `core_chat` conversations.

## Mapping

For current core chat conversations:

```text
chat::ConversationId(protocol, channel, peer != 0)
    -> ui::chat::ConversationId {
         kind = DirectPeer,
         protocol = map(protocol),
         primary = peer,
         secondary = channel
       }

chat::ConversationId(protocol, channel, peer = 0)
    -> ui::chat::ConversationId {
         kind = Channel,
         protocol = map(protocol),
         primary = channel,
         secondary = 0
       }
```

Team presentation rows are represented as `ConversationKind::Team`. They are not
forced into `chat::ConversationId`.

## Message Mapping

Message references and delivery labels are presentation projections. The
`chat_presentation_adapters` layer may map `chat::MessageStatus` to
`ui::chat::MessageDeliveryState` and `chat::ChatMessage` to
`ui::chat::MessageRef`, but it must not treat that reference as a database row
id, retry token, pending nonce, or packet id unless that source field is
explicitly available.

Current adapter mapping is intentionally narrow:

```text
chat::MessageStatus::Incoming -> MessageDeliveryState::Received
chat::MessageStatus::Queued   -> MessageDeliveryState::Queued
chat::MessageStatus::Sent     -> MessageDeliveryState::Sent
chat::MessageStatus::Failed   -> MessageDeliveryState::Failed
```

Send failure mapping from Mesh/Core send results is deferred until the
pending/failure ownership is explicit.

The current compatibility mapper copies `chat::ChatMessage::msg_id` into
`MessageRef::protocol_id`. That is a compatibility projection only. It is not
a stable local UI id, database row id, retry nonce, or pending token. When a
message repository exposes a stable local row id or message handle, the source
adapter should populate `MessageRef::local_id` and keep protocol packet ids in
`MessageRef::protocol_id`.

## Workspace Model Contract

`ChatWorkspaceModel` is a thin presentation model. It keeps the currently
selected `ui::chat::ConversationId`, conversation list offset, and message list
offset. Its read path is:

```text
ChatWorkspaceModel::snapshot()
    -> ChatWorkspaceRequest
    -> IChatPresentationSource::buildChatWorkspaceSnapshot(...)
```

Its action path is:

```text
ChatWorkspaceModel::selectConversation(...)
ChatWorkspaceModel::sendMessage(...)
ChatWorkspaceModel::markRead(...)
    -> IChatActionSink
```

`sendMessage` sends against the selected UI conversation token. Mapping that UI
token back to `core_chat` identity, `MeshSession`, or legacy send behavior
belongs to the Source/Sink adapter layer, not to `ui_presentation`.

## Source/Sink Adapter Contract

Chat presentation adapters are the first layer allowed to touch real chat
services. A Source may read `ChatService`, `ContactService`, `TeamService`, and
mapper adapters to build `ChatWorkspaceSnapshot`. An ActionSink may translate
`SendMessageView`, `selectConversation`, and `markRead` into app service
commands.

The read and write sides stay separate:

```text
ChatService / ContactService / TeamService
    -> IChatPresentationSource
        -> ChatWorkspaceSnapshot

SendMessageView / ConversationId action
    -> IChatActionSink
        -> ChatService / MeshSession / TeamService
```

Source adapters must not send messages or mutate renderer state. ActionSink
adapters must not build snapshots or format row labels. Renderer code must not
call these services directly.

The initial legacy sink supports `DirectPeer` and `Channel` conversations.
`Team`, `Broadcast`, and `System` return `Unsupported` until their semantics are
explicitly mapped in later phases.

The current Phase 5.6-c compatibility adapter lives in
`modules/ui_shared/src/ui/presentation_sources/chat_presentation_source.cpp`
and
`modules/ui_shared/src/ui/presentation_sources/legacy_chat_action_sink.cpp`.
It is a Source/Sink boundary around the existing `ChatService`, not a new chat
domain model.

## Minimal Renderer Connection

Phase 5.6-d may connect an existing renderer surface to `ChatWorkspaceModel`
for the primary conversation list, selected conversation, message list, send,
and mark-read paths. During transition, legacy LVGL widgets can still require
local compatibility conversion back to `chat::ConversationMeta` or
`chat::ChatMessage`. That compatibility conversion is not a public identity
adapter and must not be treated as part of the portable presentation contract.

Any remaining renderer access to `ChatService`, `ContactService`, team runtime,
or event-specific message lookup is migration debt for later hardening. New
renderer paths must use:

```text
Renderer -> ChatWorkspaceModel -> Source/Sink
```

## Non-Goals

Phase 5.6 does not complete team chat behavior, does not change message
storage, does not redefine `chat::ConversationId`, and does not change the
underlying send/receive protocol paths.
