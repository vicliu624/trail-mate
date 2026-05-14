# Chat UI Controller Legacy Ownership Audit

## Current Role

`ChatUiController` is still a legacy application controller.

It owns more than rendering:

- LVGL screen state machine
- chat event handling
- `ChatService` processIncoming / flushStore calls
- Team location/command legacy path
- key verification modal
- team position picker
- conversation cache
- compose / IME lifecycle

## Migrated in Phase 5.6

The non-team direct/channel path has started moving to `ChatWorkspaceModel`.
Team text projection/send has started migrating to
`TeamChatPresentationSource` / `TeamChatActionSink`.

Migrated surfaces:

- selecting non-team conversation calls `ChatWorkspaceModel::selectConversation`
- non-team conversation message view can be populated from `ChatWorkspaceSnapshot`
- non-team send action calls `ChatWorkspaceModel::sendMessage`
- mark read can call `ChatWorkspaceModel::markRead`
- Team text projection calls the dedicated Team `ChatWorkspaceModel`
- Team text send calls `TeamChatActionSink` through the dedicated Team
  `ChatWorkspaceModel`

## Remaining Legacy Ownership

The following remain legacy-owned:

- Team location/command send
- richer Team location/command payload UI
- Team structured pending/failure delivery
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

## Renderer Hardening in Phase 5.9

Phase 5.9 locks the migrated Chat paths without claiming
`ChatUiController` is clean.

Regression guard:

- non-team selection must continue using `chat_model_.selectConversation`
- non-team message rendering must continue using `chat_model_.buildSnapshot`
  into controller-owned snapshot buffers
- non-team send must continue using `chat_model_.sendMessage`
- non-team read clearing must continue using `chat_model_.markRead`
- Team text projection/send must continue using `team_chat_model_`

Allowed legacy ownership remains:

- `ChatService` lifecycle and compatibility refresh calls
- key verification modal
- event bus handling
- conversation cache
- Team location/command picker and send path
- platform GPS/team helper usage

Do not add new renderer send/read logic that bypasses `ChatWorkspaceModel`.

Do not reintroduce `chat_model_.snapshot()` or `team_chat_model_.snapshot()`
inside `ChatUiController`; `ChatWorkspaceSnapshot` is large enough to overflow
ESP `loopTask` when copied on the stack during page entry.

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
