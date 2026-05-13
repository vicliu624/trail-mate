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

## Non-Goals

Phase 5.6-pre does not add `ChatWorkspaceModel`, does not connect LVGL chat
pages, does not change team chat behavior, does not change message storage, and
does not change send/receive paths.
