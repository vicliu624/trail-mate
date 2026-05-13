# Team Action Ownership Audit

## Purpose

Phase 7.2 establishes runtime/action ownership for Team text, location, and
command actions.

Team actions are command ownership, not renderer ownership and not
`ChatWorkspaceModel` ownership.

## Current State

| Action | Current owner | Current UI path | Problem |
| --- | --- | --- | --- |
| Team text | `TeamChatActionSink` | Team chat compose path | mostly migrated |
| Team location marker | `ChatUiController::sendTeamLocationWithIcon` | Team position picker | GPS read, key setup, payload encoding, send, and log append are mixed with UI |
| Team command | no active send path in Chat UI; read formatting exists | command payload display path | command encoding ownership is not explicit |
| Team log append | `TeamUiStore` runtime helpers | mixed Team/chat handlers | outgoing projection is not uniformly owned by Team action runtime |

## Current Team Text Send

Team text send flows through:

```text
ChatUiController
  -> team_chat_model_.sendMessage(...)
    -> TeamChatActionSink
      -> ITeamChatCommandPort
      -> TeamController::onChat(...)
```

This is the desired Phase 7.2 baseline for text.

## Current Location Marker Send

Team location marker send currently lives in the chat UI controller:

```text
ChatUiController::sendTeamLocationWithIcon(...)
  -> platform GPS runtime read
  -> TeamUiStore snapshot
  -> TeamController key setup
  -> TeamChatLocation payload encoding
  -> TeamController::onChat(...)
  -> TeamUiStore chat log append
```

This is too much runtime behavior inside a renderer/controller surface.

## Current Command Send

The chat UI currently formats incoming or logged Team command payloads, but no
active command send entry point is wired in the Chat UI.

Phase 7.2 should still define command action ownership so future command UI
does not add payload encoding back to `ChatUiController`.

## Boundary Rule

Renderer/controller code may collect user input but must not:

- encode Team payloads
- directly send Team runtime packets
- set Team protocol keys
- append outgoing Team location/command logs
- map Team to DirectPeer or Channel

## Target Ownership

Team actions are owned by:

```text
TeamActionSink / LegacyTeamActionBridge
```

Team payload encoding is owned by:

```text
TeamPayloadEncoder / protocol adapter
```

Team read projection remains:

```text
TeamChatPresentationSource
```

## Phase 7.2 Decision

Phase 7.2 migrates Team location send behind an explicit Team action sink and
bridge.

The chat UI submits `TeamActionRequest` with marker intent. Current location
resolution, Team key setup, payload encoding, Team runtime send, and outgoing
Team log append move behind `LegacyTeamActionBridge` and its runtime ports.

Phase 7.2 defines Team command action types and bridge behavior, but the
current Chat UI command send path remains unsupported until a real command UI
surface exists.

Phase 7.2 does not implement rich Team payload rendering.

## Remaining Legacy

The following remain bounded legacy:

- Team position picker widgets
- Team rich payload rendering
- command UI creation
- Team key verification modal
- Team payload display formatting in existing chat/team views
