# Legacy Burn-down Register

## Purpose

This register tracks legacy surfaces that are now contained by Phase 7 runtime ownership boundaries.

A legacy surface may remain temporarily only if it has:

- a new owner
- remaining caller list
- deletion or rename condition
- target removal phase
- checker status

## Chat / Team Legacy Surfaces

| Legacy surface | New owner | Remaining callers | Removal condition | Target phase | Status |
| --- | --- | --- | --- | --- | --- |
| `LegacyChatDeliveryEventBridge` | `IChatDeliveryEventPort` / `ChatDeliveryEventProjector` | `ChatPageRuntimeEventPump::handleChatSendResult(...)`; `chat_page_runtime.cpp` owns bridge lifetime | EventBus/runtime send-result schema is structured enough to publish delivery events without legacy mapper bridge | 7.x | contained |
| `LegacyChatDeliveryActionBridge` | `ChatDeliveryActionService` / `IChatDeliveryActionSink` | No visible message-menu UI entry yet; smoke tests exercise the bridge | UI submits `ChatDeliveryActionRequest` directly or bridge is renamed as a protocol/runtime adapter | 7.x | contained |
| `LegacyTeamActionBridge` | `TeamActionRequest` / `ITeamActionSink` / Team runtime command port | `ChatUiController::sendTeamLocationWithIcon(...)` submits `LocationMarker` intent; `chat_page_runtime.cpp` owns bridge lifetime | `TeamActionService` replaces the legacy bridge and renderers/controllers only submit `TeamActionRequest` | 7.x | contained |
| `LegacyKeyVerificationSource` | `KeyVerificationModel` / `IKeyVerificationPresentationSource` | `ChatPageRuntimeEventPump` forwards key verification events into source/session | Protocol-specific key verification source is renamed or split into non-legacy adapter(s) | 7.x | contained |
| `LegacyKeyVerificationActionSink` | `KeyVerificationModel` / `IKeyVerificationActionSink` | `KeyVerificationModel` calls action sink; `chat_page_runtime.cpp` owns sink lifetime | Protocol-specific adapter is renamed or split into `MeshKeyVerificationActionSink` / `MeshtasticKeyVerificationActionSink` | 7.x | contained |
| `ChatUiController` key verification modal rendering | `KeyVerificationModalRenderer` helper consumes `KeyVerificationSnapshot` | `ChatUiController` opens/closes modal and forwards submit/trust callbacks | Full modal view object owns widget lifecycle and controller keeps only workflow routing | 7.x | burned-down |
| `ChatUiController` Team payload encoding | `LegacyTeamActionBridge` | None expected for send path; controller submits `TeamActionRequest` only | Checker forbids `encodeTeamChatLocation` / `encodeTeamChatCommand` / raw `TeamChatMessage` send encoding in controller | 7.6 | burned-down |
| `ChatUiController` delivery mutation | `ChatDeliveryReadModel` / `ChatDeliveryActionService` | None expected; event pump forwards send-result events to delivery bridge | Checker forbids direct `ChatDeliveryReadModel`, `ChatDeliveryEventProjector`, `ChatDeliveryActionService`, and `LegacyChatDeliveryEventBridge` ownership in controller | 7.6 / 7.7 | burned-down |
| `ChatUiController` runtime event pump | `ChatPageRuntimeEventPump` / `ChatPageRuntimeFacade` | None expected; app facade registers runtime facade instead of controller | Checker forbids `ChatUiController::onChatEvent(...)`, `processIncoming()`, `flushStore()`, and key verification source projection calls in controller | 7.7 | burned-down |
| `ChatUiController` Team rich payload formatting | `TeamRichPayloadProjector` / `TeamChatPresentationSource` | None in `ChatUiController`; Team chat rows consume projected summaries from `team_chat_model_.snapshot()` | Checker forbids `format_team_chat_entry(...)`, `decodeTeamChatLocation(...)`, `decodeTeamChatCommand(...)`, `TeamChatLocation`, and `TeamChatCommand` in controller | 7.8 | burned-down |
| `ChatUiController` Team position picker renderer | `TeamPositionPickerRenderer` | `ChatUiController` calls open/close/updateHint and handles selected/cancel workflow only | Future UX pack-specific picker replaces shared LVGL renderer or renderer is renamed as official chat picker view | 7.9 | burned-down |
| `MessageRow` Team rich display limitations | `TeamRichPayloadDisplay` / future Team row renderer | `TeamChatPresentationSource` projects rich payloads to summary text for current renderer compatibility | Rich Team row/card renderer consumes structured display fields directly | 7.x | contained |

## Checker Status

| Surface | Checker rule |
| --- | --- |
| Team send payload encoding in controller | forbidden |
| Direct key verification runtime API in controller | forbidden |
| Direct delivery read/action ownership in controller | forbidden |
| Key verification modal helper | required |
| Runtime event pump in controller | forbidden |
| Team rich payload formatting in controller | forbidden |
| Team position picker widget refs in controller | forbidden |
| Legacy bridges without removal condition | forbidden by register token check |
