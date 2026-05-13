# Team Action Runtime Specification

## Purpose

Define runtime/action ownership for Team text, location marker, and command
actions.

Team location and command actions are not Chat presentation state. They are Team
runtime commands that may be initiated by a chat UI surface.

## Core Rule

Team action payload encoding belongs to Team action/runtime adapters.

```text
renderer/controller input
  -> TeamActionRequest
    -> ITeamActionSink
      -> LegacyTeamActionBridge
        -> Team payload encoder / Team runtime command port
```

## Types

Phase 7.2 defines:

- `TeamActionKind`
- `TeamCommandKind`
- `TeamLocationMarkerRequest`
- `TeamLocationSnapshot`
- `TeamCommandRequest`
- `TeamActionRequest`
- `ITeamActionSink`
- `ITeamLocationSource`
- `LegacyTeamActionBridge`

## Ownership

`TeamActionRequest` is a command DTO. It describes the user intent and must not
contain renderer widgets, LVGL objects, raw packets, or app service references.

`ITeamActionSink` handles Team actions. It must not build chat snapshots.

`LegacyTeamActionBridge` translates Team action requests into the current Team
runtime command port and Team UI store compatibility helpers.

`ITeamLocationSource` provides current location facts to Team action adapters
when a renderer submits `use_current_location`. It is a runtime source, not a UI
widget dependency.

`TeamChatPresentationSource` remains a read projection and must not send Team
actions.

`ChatUiController` may collect input and submit `TeamActionRequest`, but it must
not encode Team payloads or directly send Team runtime packets.

## Text

Team text continues to flow through `TeamChatActionSink` and
`team_chat_model_.sendMessage(...)`.

`LegacyTeamActionBridge` may support text for command sink completeness, but the
existing Team chat compose path remains owned by `TeamChatActionSink`.

## Location Marker

Location marker send is represented as:

```text
TeamActionKind::LocationMarker
TeamLocationMarkerRequest
```

The request contains location facts and marker identity. Encoding
`TeamChatLocation` and appending the outgoing structured Team log belong to the
Team action bridge.

When the UI wants to send the current location, it submits
`use_current_location` and marker identity. The bridge resolves the current
location through `ITeamLocationSource`; the renderer/controller must not read
GPS runtime directly for this action.

## Command

Command send is represented as:

```text
TeamActionKind::Command
TeamCommandRequest
```

Phase 7.2 defines the command request and bridge boundary. If no concrete
command UI/runtime path is available, the bridge may return `Unsupported`
rather than encoding command payload in a renderer.

## Non-Goals

Phase 7.2 does not implement rich Team payload UI.

Phase 7.2 does not move Team text out of `TeamChatActionSink`.

Phase 7.2 does not turn Team into DirectPeer or Channel chat.

Phase 7.2 does not rewrite `ChatUiController`.

Phase 7.2 does not change Team wire format.
