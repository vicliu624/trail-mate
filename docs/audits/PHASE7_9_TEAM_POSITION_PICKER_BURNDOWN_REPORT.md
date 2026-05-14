# Phase 7.9 Team Position Picker Burn-down Report

## Decision

Phase 7.9 moved Team position picker LVGL widget lifecycle out of `ChatUiController` and into `TeamPositionPickerRenderer`.

This was a renderer / widget lifecycle burn-down. It did not change Team action send ownership.

## Burned Down

| Surface | Result |
| --- | --- |
| `team_position_picker_overlay_` in controller | removed |
| `team_position_picker_panel_` in controller | removed |
| `team_position_picker_desc_` in controller | removed |
| `team_position_picker_group_` in controller | removed |
| `team_position_prev_group_` in controller | removed |
| `team_position_icon_ctxs_` in controller | removed |
| `TeamPositionIconEventCtx` in controller | removed |
| `team_position_icon_event_cb(...)` in controller | removed |
| `team_position_cancel_event_cb(...)` in controller | removed |
| Team position picker LVGL widget construction in controller | moved to `TeamPositionPickerRenderer` |

## Still Contained

| Surface | Reason | Exit condition |
| --- | --- | --- |
| `sendTeamLocationWithIcon(...)` | Controller still coordinates selected marker workflow | Future Team workflow coordinator or UX pack-specific flow submits `TeamActionRequest` directly |
| `LegacyTeamActionBridge` | Team action send runtime adapter is still legacy | Replace or rename as a stable Team action service/adapter |
| `TeamPositionPickerRenderer` LVGL specificity | Shared chat UI still uses LVGL widgets | Future UX pack-specific picker replaces the shared LVGL renderer |

## Checker Changes

- Required Team position picker audit/spec/report files.
- Required `TeamPositionPickerRenderer` header and source.
- Forbid picker LVGL refs and icon event contexts in `ChatUiController`.
- Forbid picker LVGL event callbacks in `ChatUiController`.
- Forbid direct `lv_obj_create(parent_)` picker construction in `ChatUiController`.
- Forbid Team action, GPS, Team store, Chat service, and payload encoding tokens in `TeamPositionPickerRenderer`.

## Remaining Work

| Work | Direction |
| --- | --- |
| Team workflow coordination | Move selection workflow out of `ChatUiController` only if a broader workflow coordinator becomes worthwhile |
| Team action bridge burn-down | Replace or rename `LegacyTeamActionBridge` after Team runtime action service boundary is stable |
| UX Pack picker variant | Allow a target-specific picker renderer to replace the shared LVGL renderer |
