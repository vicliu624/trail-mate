# Phase 7.8 Team Rich Payload Burn-down Report

## Decision

Phase 7.8 moved Team location/command display decoding out of `ChatUiController` and into Team presentation projection.

The first-phase display remains text-compatible with existing `MessageRow` rendering, but the owner is now explicit:

```text
TeamChatLogEntry -> TeamRichPayloadProjector -> TeamChatPresentationSource -> MessageRow text
```

## Burned Down

| Surface | Result |
| --- | --- |
| `ChatUiController` direct Team rich payload formatter | removed |
| `format_team_chat_entry(...)` in chat controller | forbidden |
| `decodeTeamChatLocation(...)` in chat controller | forbidden |
| `decodeTeamChatCommand(...)` in chat controller | forbidden |
| `TeamChatLocation` / `TeamChatCommand` display structs in chat controller | forbidden |
| Team chat subtitle/message rich display summary | projected by `TeamRichPayloadProjector` through `TeamChatPresentationSource` |
| `TeamRichPayloadProjector` heap formatting temporaries | removed; summary construction uses fixed buffers and bounded copies |

## Still Contained

| Surface | Reason | Exit condition |
| --- | --- | --- |
| `LegacyTeamActionBridge` | Send-side Team action runtime bridge remains legacy | Introduce or rename a stable Team action service/adapter |
| Team position picker renderer | Picker widget lifecycle is separate from display projection | Resolved by Phase 7.9 with `TeamPositionPickerRenderer` |
| `MessageRow` rich display limitations | Existing renderer only consumes text rows | Future Team row model or renderer supports rich payload display fields |
| Other Team rich decode surfaces | Contacts/GPS/team screens have separate legacy display paths | Later phases migrate those screens to shared Team projection |
| `TeamChatPresentationSource` vector boundary | Legacy Team store API returns recent logs through `std::vector` | Add a fixed-capacity visitor / bounded iterator API to `TeamUiStore` |

## Checker Changes

- Added Team rich payload presentation audit/spec requirements.
- Required `TeamRichPayloadDisplay` and `TeamRichPayloadProjector`.
- Required projector smoke test.
- Required `TeamChatPresentationSource` to consume `TeamRichPayloadProjector`.
- Forbid Team rich payload decode/format tokens in `ChatUiController`.
- Forbid send/action/runtime tokens in `TeamRichPayloadProjector`.
- Forbid `std::string`, `std::vector`, `iostream`, `sstream`, and `<format>` in `TeamRichPayloadProjector`.

## Remaining Work

| Work | Direction |
| --- | --- |
| Rich Team card UI | Add renderer support for structured `TeamRichPayloadDisplay` fields |
| Team position picker renderer | Resolved by Phase 7.9 without changing Team send ownership |
| Contacts/GPS Team payload formatting | Move those legacy display paths to shared Team presentation projection |
| Team action bridge burn-down | Replace or rename `LegacyTeamActionBridge` when runtime service boundary is stable |
| Team store recent-log API | Replace vector-return API with fixed-capacity visitor / bounded iterator for embedded targets |
