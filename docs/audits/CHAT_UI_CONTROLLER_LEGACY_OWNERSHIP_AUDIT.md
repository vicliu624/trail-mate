# Chat UI Controller Legacy Ownership Audit

## Current Role

`ChatUiController` is still a legacy application controller.

It owns more than rendering:

- LVGL screen state machine
- chat event handling
- `ChatService` processIncoming / flushStore calls
- Team chat path
- key verification modal
- team position picker
- conversation cache
- compose / IME lifecycle

## Migrated in Phase 5.6

The non-team direct/channel path has started moving to `ChatWorkspaceModel`.

Migrated surfaces:

- selecting non-team conversation calls `ChatWorkspaceModel::selectConversation`
- non-team conversation message view can be populated from `ChatWorkspaceSnapshot`
- non-team send action calls `ChatWorkspaceModel::sendMessage`
- mark read can call `ChatWorkspaceModel::markRead`

## Remaining Legacy Ownership

The following remain legacy-owned:

- Team conversation projection
- Team text/location/command send
- key verification modal
- direct event bus handling
- conversation list cache
- contact title fallback logic
- `ChatService` processIncoming / flushStore lifecycle
- unread refresh logic
- team position picker
- platform GPS/team location helper usage

## Constraint

New non-team chat rendering must prefer `ChatWorkspaceModel`.

New direct calls from renderer/controller to `ChatService` must be justified as
legacy ownership or moved behind source/sink.

## Non-Goal

Phase 5.6-closeout does not remove `ChatService&` from `ChatUiController`.

That requires a later split between:

- ChatApplicationController
- ChatRenderer
- TeamChatPresentation
- KeyVerificationPresentation

## Pattern

This is a legacy island / strangler boundary. The controller is not clean yet;
the important closeout outcome is that its ownership is explicit and new
generic chat presentation work has a preferred path through
`ChatWorkspaceModel`.
