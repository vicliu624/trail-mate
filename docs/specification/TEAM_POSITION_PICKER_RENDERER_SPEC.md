# Team Position Picker Renderer Spec

## Rule

Picker rendering belongs to `TeamPositionPickerRenderer`.

Team action sending belongs to `ITeamActionSink`.

`ChatUiController` coordinates the selection workflow but must not directly create or store Team position picker widgets.

## Objects

| Object | Pattern | Responsibility | Forbidden |
| --- | --- | --- | --- |
| `TeamPositionPickerRenderer` | Passive View / Widget Renderer | Create, close, update, and own LVGL picker widgets | Team action send, GPS read, payload encoding |
| `TeamPositionPickerRenderer::Callbacks` | Callback Port | Report icon selected / cancel to workflow owner | `ChatService`, `TeamUiStore`, action sink ownership |
| Icon event context | View State | Connect LVGL icon button events to renderer callbacks | Runtime payload or send state |
| `ChatUiController` | Workflow Coordinator | Open picker, receive picker result, submit selected marker through Team action sink | Direct picker widget construction |
| `ITeamActionSink` | Command Sink | Send Team location marker action | Render picker widgets |

## `TeamPositionPickerRenderer`

`TeamPositionPickerRenderer` is constructed with:

- `lv_obj_t* parent`
- `TeamPositionPickerRenderer::Callbacks`

It exposes:

- `bool open()`
- `void close(bool restore_group)`
- `bool isOpen() const`
- `void updateHint(uint8_t icon_id)`

It owns:

- overlay
- panel
- description label
- picker LVGL group
- previous LVGL group reference
- icon event contexts
- icon and cancel LVGL callbacks

It may use LVGL and page profile helpers because this object is intentionally a widget renderer.

## Callback Contract

`Callbacks::on_icon_selected` receives:

- `void* user_data`
- `uint8_t icon_id`

`Callbacks::on_cancel` receives:

- `void* user_data`

The renderer invokes callbacks only to report picker results. It must not interpret the result as a Team send operation.

## Controller Contract

`ChatUiController` may call:

- `team_position_picker_->open()`
- `team_position_picker_->close(...)`
- `team_position_picker_->updateHint(...)`
- `team_position_picker_->isOpen()`

`ChatUiController` may keep:

- `sendTeamLocationWithIcon(...)`
- `onTeamPositionIconSelected(...)`
- `onTeamPositionCancel(...)`

`sendTeamLocationWithIcon(...)` remains valid only because it submits `TeamActionRequest` to `ITeamActionSink`. It must not encode Team payloads directly.

## Non-Goals

Phase 7.9 does not:

- change Team action send path
- change Team protocol
- add Map overlay behavior
- change GPS source ownership
- introduce UX Pack
- change `MessageRow`
- add rich Team cards
- remove `sendTeamLocationWithIcon(...)`
