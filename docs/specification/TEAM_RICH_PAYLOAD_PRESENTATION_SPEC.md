# Team Rich Payload Presentation Specification

## Purpose

Define the Phase 7.8 ownership boundary for Team location and command display projection.

## Core Rule

Team rich payload display is presentation projection, not controller formatting.

Team payload encoding belongs to Team action/runtime adapters. Team payload decoding for chat display belongs to a Team presentation projector.

## Types

Phase 7.8 defines:

- `TeamRichPayloadKind`
- `TeamCommandDisplayKind`
- `TeamLocationDisplay`
- `TeamCommandDisplay`
- `TeamRichPayloadDisplay`
- `TeamRichPayloadProjector`

## TeamRichPayloadDisplay

`TeamRichPayloadDisplay` is a UI-ready read DTO.

It may contain:

- display kind
- title
- summary text
- badge text
- location display fields
- command display fields

It must not contain:

- raw packet bytes
- PSK material
- raw Team protocol message ownership
- LVGL widgets
- send/retry action state

## TeamRichPayloadProjector

`TeamRichPayloadProjector` maps:

```text
TeamChatLogEntry -> TeamRichPayloadDisplay
```

It may call legacy Team payload decode helpers because Phase 7.8 is an anti-corruption display projection.

It must remain portable shared code. Shared portable code must remain C++11-compatible unless explicitly marked target-local. Target-local code may use a higher standard provided by its target toolchain.

It must not:

- render LVGL widgets
- access `ChatUiController`
- call `sendTeamAction(...)`
- encode Team location or command payloads
- mutate `TeamUiStore`
- build `ChatWorkspaceSnapshot`
- map Team chat to DirectPeer or Channel chat
- allocate display summaries through `std::string`
- use `std::vector`, `iostream`, `sstream`, or `<format>` for projection formatting

## Embedded Hardening

`TeamRichPayloadProjector` must build summary text with fixed buffers, `std::snprintf`, bounded copies, and `ui::copyText`.

The projector may read legacy payload strings after decode, but it must not allocate temporary strings while projecting display rows.

## Team Row Projection

`TeamChatPresentationSource` consumes `TeamRichPayloadProjector`.

For Phase 7.8, it projects `TeamRichPayloadDisplay::summary` into existing `MessageRow::text` and conversation subtitle fields.

This keeps compatibility with the current chat renderer while making the display decode owner explicit.

## Controller Rule

`ChatUiController` may:

- select Team conversation UI
- render rows from `team_chat_model_.snapshot()`
- open Team position picker UI
- submit Team action requests

`ChatUiController` must not:

- define `format_team_chat_entry(...)`
- call `decodeTeamChatLocation(...)`
- call `decodeTeamChatCommand(...)`
- construct `TeamChatLocation` or `TeamChatCommand` for display formatting
- format Team rich payload rows from raw Team payloads

## Non-goals

Phase 7.8 does not:

- change Team protocol packet format
- change Team action send path
- replace `LegacyTeamActionBridge`
- change `TeamUiStore` schema
- add Map overlays
- implement full Team rich cards
- redesign `MessageRow`
- rewrite `ChatWorkspaceModel`
- replace the legacy `TeamUiStore` recent-log API

## Legacy Store Boundary

`TeamChatPresentationSource` still consumes `std::vector<TeamChatLogEntry>` because the legacy `TeamUiStore` recent-log API returns a vector.

Exit condition:

```text
TeamUiStore exposes a fixed-capacity recent-log visitor or equivalent bounded iterator.
```

C++11 has no `std::span`, so a future portable API should prefer an explicit visitor/callback over introducing a higher-standard dependency into shared embedded UI code.
