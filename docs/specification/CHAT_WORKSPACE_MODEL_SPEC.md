# Chat Workspace Model Specification

## Purpose

`ChatWorkspaceModel` is a presentation-layer ViewModel.

It owns UI-local workspace state only:

- selected conversation
- conversation list offset
- message list offset

It does not own:

- ChatService
- ContactService
- TeamService
- MeshSession
- message repository
- pending queue
- ACK tracker
- retry state
- failure inference

## Pattern

This model follows:

- MVVM / Passive View
- Source/Sink Port
- Immutable Snapshot
- CQRS Read/Command split

## Snapshot Flow

Renderer calls:

```text
ChatWorkspaceModel::snapshot()
    -> IChatPresentationSource::buildChatWorkspaceSnapshot(request, out)
```

The returned `ChatWorkspaceSnapshot` is an immutable value object for UI
rendering.

## Action Flow

Renderer calls:

```text
ChatWorkspaceModel::selectConversation(id)
ChatWorkspaceModel::sendMessage(text)
ChatWorkspaceModel::markRead(id)
```

The model forwards actions to `IChatActionSink`.

## Optimistic Selection

`selectConversation(id)` uses optimistic UI selection.

Semantics:

1. Validate the presentation `ConversationId`.
2. Update local `selected_conversation`.
3. Reset `message_offset`.
4. Notify `IChatActionSink::selectConversation(id)`.
5. Do not roll back local selection if the sink returns failure.

Rationale:

- selected conversation is presentation-local state.
- sink selection is a synchronization hook for business-side side effects.
- unsupported conversations may still be visible/selectable in UI.

## Message Paging

`ChatWorkspaceRequest::message_offset` is reserved for future message paging.

`ChatPresentationSource` currently ignores `message_offset` and returns the
recent message window.

This is intentional. The closeout phase must not change `ChatService` storage
or paging behavior.

Renderers must not assume `message_offset` is already honored by the chat read
projection.

## Source/Sink Adapter Contract

`ChatPresentationSource` is the product chat read projection adapter. It may
read `ChatService` and `ContactService`, and may use
`chat_presentation_adapters` to map core chat types into `ui_presentation`
rows.

It must not:

- send messages
- mark conversations read
- mutate `ChatService`
- access LVGL widgets
- access radio, mesh adapters, PKI, or packet builders

`LegacyChatActionSink` is the Phase 5.6 compatibility command adapter. It may
translate UI actions into `ChatService` commands.

It must not:

- build `ChatWorkspaceSnapshot`
- format UI labels
- access LVGL widgets
- inspect renderer state
- build radio packets or perform PKI logic

## Pending / Failure

`ChatWorkspaceModel` must not own pending messages, ACK tracking, retry state,
or failure inference.

Pending/failure projection must flow through:

```text
ChatService / MeshSession / ACK tracker / pending store
    -> IChatPresentationSource
        -> MessageRow.delivery
        -> MessageRow.failure
```

Until structured failure ownership is explicit, the compatibility projection is
limited to the coarse `chat::MessageStatus` available from `ChatMessage`.

## Team Chat

Team chat is not part of the generic DirectPeer/Channel path.

Team rows use:

```text
ConversationKind::Team
```

Team send/read/projection semantics are handled by a dedicated Team Chat
Presentation phase.

## Contract Role

This file is an Architecture Decision Record and ViewModel contract. It
constrains later implementation; it is not only descriptive documentation.
