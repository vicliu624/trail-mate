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

## Non-Goals

Phase 5.6 does not connect LVGL chat pages, does not change team chat behavior,
does not change message storage, and does not change send/receive paths.
