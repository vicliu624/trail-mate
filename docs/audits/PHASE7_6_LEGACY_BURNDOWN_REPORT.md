# Phase 7.6 Legacy Burn-down Report

## Decision

Phase 7.6 reduced Chat / Team / key-verification legacy ownership surfaces without introducing new runtime models.

The pass burned down legacy paths already covered by Phase 7 owners and documented exit conditions for bridges that must remain temporarily.

## Burned Down

| Surface | Result |
| --- | --- |
| `ChatUiController` direct Team payload encoding | forbidden; controller submits `TeamActionRequest` for location marker sends |
| `ChatUiController` direct key verification runtime calls | forbidden; controller calls `KeyVerificationModel` only |
| `ChatUiController` direct delivery ownership | forbidden; controller does not own read model, projector, or action service |
| Key verification modal rendering | extracted to `KeyVerificationModalRenderer` helper consuming `KeyVerificationSnapshot` |
| Legacy key verification API token in controller | controller UI method renamed to avoid `submitKeyVerificationNumber` ownership token |

## Still Contained

| Surface | Reason | Exit condition |
| --- | --- | --- |
| `LegacyChatDeliveryEventBridge` | EventBus still reaches `ChatUiController` first for chat send result refresh | Runtime event pump forwards `ChatSendResultEvent` directly |
| `LegacyChatDeliveryActionBridge` | No visible retry/cancel/clear-failure message menu exists yet | UI submits `ChatDeliveryActionRequest` directly or bridge is renamed as a formal adapter |
| `LegacyTeamActionBridge` | Team runtime command port still wraps legacy helper APIs | Introduce a non-legacy `TeamActionService` / command port |
| `LegacyKeyVerificationSource` / `LegacyKeyVerificationActionSink` | MeshCore / Meshtastic verification APIs still differ behind adapter | Split or rename protocol-specific adapters |
| `ChatUiController` | Still coordinates legacy LVGL screen, event forwarding, conversation cache, and Team rich text formatting | Future controller thinning moves event pump and rich payload projection out |

## Checker Changes

- Added `LEGACY_BURNDOWN_REGISTER.md`, `CHAT_UI_CONTROLLER_BURNDOWN_AUDIT.md`, and this closeout report as required Phase 7 files.
- Added burn-down register token checks for remaining callers, removal condition, target phase, and status.
- Required key verification modal renderer helper files.
- Forbid direct Team send payload encoding tokens in `ChatUiController`.
- Forbid direct key verification runtime API tokens in `ChatUiController`.
- Forbid direct delivery read model / projector / action service ownership in `ChatUiController`.
- Narrowed legacy API token survival to legacy adapter/runtime paths.

## Remaining Work

| Work | Owner direction |
| --- | --- |
| Event pump extraction | `chat_page_runtime.cpp` or app shell should forward runtime events before controller refresh |
| Team rich payload rendering | Team presentation adapter should project location/command display fields |
| Key verification modal full view split | A fuller LVGL view object can own modal lifecycle beyond helper functions |
| ChatUiController thinning | Conversation cache and store flush should move to runtime/app shell owners |
