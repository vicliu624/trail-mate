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
ChatUiController legacy team path
```

Current functions include:

- team title resolution
- team conversation refresh
- team unread clearing
- team text rendering
- team location rendering
- team command rendering
- team position picker
- send team location marker
- team compose path

## Target Presentation Identity

Team rows must use:

```text
ui::chat::ConversationKind::Team
ui::chat::ChatProtocolKind::TrailMate or Mixed
```

Team conversation must not be converted through `toCoreConversationId`.

## Target Read Side

Introduce later:

```text
TeamChatPresentationSource
```

or:

```text
LegacyTeamChatPresentationSource
```

It will project:

- team text entries
- team location entries
- team command entries
- team unread state
- team title
- team sender labels
- team delivery state where available

## Target Write Side

Introduce later:

```text
TeamChatActionSink
```

It will handle:

- send team text
- send team location marker
- send team command

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
