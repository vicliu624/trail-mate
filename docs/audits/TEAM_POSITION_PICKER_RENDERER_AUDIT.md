# Team Position Picker Renderer Audit

## Purpose

Phase 7.9 burns down Team position picker widget lifecycle from `ChatUiController`.

This is a renderer / widget lifecycle boundary. It is not Team action ownership, Team protocol ownership, GPS source ownership, or map overlay ownership.

## Current Ownership Before 7.9

| Surface | Previous owner | Problem | Target owner |
| --- | --- | --- | --- |
| Picker overlay widget | `ChatUiController` | Controller stored LVGL modal widget refs | `TeamPositionPickerRenderer` |
| Picker panel widget | `ChatUiController` | Controller created and destroyed picker widget tree | `TeamPositionPickerRenderer` |
| Picker description label | `ChatUiController` | Controller owned hint label state | `TeamPositionPickerRenderer` |
| Picker focus group | `ChatUiController` | Controller owned LVGL group switching | `TeamPositionPickerRenderer` |
| Previous focus group | `ChatUiController` | Controller owned restore target | `TeamPositionPickerRenderer` |
| icon event contexts | `ChatUiController` | Controller stored per-button LVGL callback context | `TeamPositionPickerRenderer` |
| Icon selected callback | `ChatUiController` static LVGL callback | Controller handled widget event routing directly | `TeamPositionPickerRenderer` callback port |
| Cancel callback | `ChatUiController` static LVGL callback | Controller handled widget event routing directly | `TeamPositionPickerRenderer` callback port |

## Boundary

`TeamPositionPickerRenderer` owns:

- opening the picker overlay
- closing and destroying the picker overlay
- creating icon buttons
- creating the cancel button
- updating hint text
- LVGL focus group setup and restore
- icon event context storage
- LVGL event callback routing

`ChatUiController` owns only:

- deciding when the Team position picker workflow should open
- receiving `icon selected` / `cancelled` results
- submitting the selected marker through the existing Team action workflow
- refreshing the current conversation after the workflow

## Non-Ownership

`TeamPositionPickerRenderer` must not:

- send Team actions
- construct `TeamActionRequest`
- access `ITeamActionSink`
- access `ChatService`
- access `TeamUiStore`
- access GPS runtime
- encode or decode Team payloads
- update `ChatWorkspaceModel`

## Decision

Phase 7.9 moves Team position picker LVGL widget lifecycle to `TeamPositionPickerRenderer`.

`ChatUiController` no longer stores picker LVGL refs or icon callback contexts. It keeps only workflow methods such as `openTeamPositionPicker(...)`, `closeTeamPositionPicker(...)`, `onTeamPositionIconSelected(...)`, and `onTeamPositionCancel(...)`.
