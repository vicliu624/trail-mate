# Team Chat Presentation Audit

## Purpose

Team chat is not a DirectPeer or Channel conversation.

It is a team-scoped collaboration projection and must not be forced into
`chat::ConversationId`.

## Current Sources

Current Team chat UI reads from:

- TeamUiStore
- TeamUiSnapshot
- TeamChatLogEntry
- TeamChatType::Text
- TeamChatType::Location
- TeamChatType::Command
- Team location marker helpers
- Team command/location encoding and decoding helpers

## Current Owner

Current owner:

```text
Team bounded presentation context, with remaining ChatUiController legacy flows
```

## Migrated in Phase 5.6-f

The following Team chat presentation surfaces have been migrated:

- Team text projection migrated to `TeamChatPresentationSource`
- Team text send migrated to `TeamChatActionSink`
- Team location entries are projected as textual `MessageRow` summaries by
  `TeamChatPresentationSource`
- Team command entries are projected as textual `MessageRow` summaries by
  `TeamChatPresentationSource`
- Team read/unread clear can be handled by `TeamChatActionSink::markRead`

## Remaining Legacy Ownership

The following remain legacy-owned:

- Team location picker UI
- Team location marker send flow
- Team command send flow
- Rich Team payload rendering beyond textual `MessageRow` summaries
- Team pending/failure structured delivery
- Team position picker LVGL overlay

## Target Presentation Identity

Team rows must use:

```text
ui::chat::ConversationKind::Team
ui::chat::ChatProtocolKind::TrailMate or Mixed
```

Team conversation must not be converted through `toCoreConversationId`.

## Target Read Side

Phase 5.6-f introduced:

```text
TeamChatPresentationSource
```

It projects:

- team text entries
- team location entries
- team command entries
- team unread state
- team title
- team sender labels
- team delivery state where available

## Target Write Side

Phase 5.6-f introduced:

```text
TeamChatActionSink
```

It handles:

- send team text

It does not yet handle:

- send team location marker
- send team command

Those remain bounded legacy/future work until their action contract and richer
payload UI are defined.

## Non-Goals Before 5.6-f

Do not:

- map Team to DirectPeer
- map Team to Channel
- send Team text through `LegacyChatActionSink`
- store Team messages as generic `ChatMessage` unless a separate design says so
- force Team location/command payload into generic `MessageRow` payload fields

## Open Questions

- Should team text be stored in ChatService history or remain TeamUiStore-owned?
- Should team location/command be rendered as MessageRow text only or get a
  richer row type later?
- Should Team failure state use MessageFailureKind or a team-specific failure
  enum?
- Does Team chat need its own pending queue?

## Pattern

This is a bounded presentation context.

```text
Team chat and Direct/Channel chat can share UI appearance,
but they do not share business identity or send semantics.
```

## Phase 5.6-f Status

The generic chat path remains DirectPeer/Channel focused. Team text now has its
own read adapter, command sink, and `ChatWorkspaceModel` instance.

Location and command entries are read-side textual projections only. Their
picker/send actions remain legacy-owned and must not be routed through
`LegacyChatActionSink`, `toCoreConversationId`, or direct/channel send paths.
