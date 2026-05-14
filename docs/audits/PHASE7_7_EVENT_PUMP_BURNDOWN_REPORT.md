# Phase 7.7 Event Pump Burn-down Report

## Decision

Phase 7.7 moved Chat runtime event projection and ChatService scheduling out of `ChatUiController`.

`chat_page_runtime.cpp` now registers a runtime facade that owns event-pump ordering. `ChatUiController` remains a UI refresh sink and legacy view coordinator.

## Burned Down

| Surface | Result |
| --- | --- |
| `ChatUiController::update()` calling `ChatService::processIncoming()` | moved to `ChatPageRuntimeEventPump::update()` |
| `ChatUiController::update()` calling `ChatService::flushStore()` | moved to `ChatPageRuntimeEventPump::update()` |
| `ChatUiController::onChatEvent(...)` delivery projection | moved to `ChatPageRuntimeEventPump::handleChatSendResult(...)` |
| `ChatUiController::onChatEvent(...)` key verification source/session update | moved to `ChatPageRuntimeEventPump` key verification handlers |
| App facade registering controller as `IChatUiRuntime` | replaced with `ChatPageRuntimeFacade` |

## Still Contained

| Surface | Reason | Exit condition |
| --- | --- | --- |
| `ChatPageRuntimeEventPump` directly calls `ChatService` | It is the Phase 7.7 runtime owner | App shell/runtime scheduler owns cadence outside page runtime |
| `ChatUiController` reads `ChatService` for UI refresh | Existing legacy screens still use `ChatMessage` and conversation cache | Chat presentation workspace fully drives LVGL conversation UI |
| Team rich payload formatting | Not part of event pump ownership | Team rich payload presentation adapter exists |
| Team position picker renderer | Still an LVGL widget flow | Extract picker renderer/helper in a later burn-down |
| Protocol mismatch UX | Still controller notification logic | Protocol support presentation model exposes UI-ready disabled reasons |

## Checker Changes

- Added `CHAT_RUNTIME_EVENT_PUMP_AUDIT.md` and `CHAT_RUNTIME_EVENT_PUMP_SPEC.md` requirements.
- Added `chat_ui_refresh_sink.h` and `chat_page_runtime_event_pump.h/.cpp` requirements.
- Forbid `processIncoming()` / `flushStore()` in `ChatUiController`.
- Forbid `delivery_event_bridge_` and key verification source projection calls in `ChatUiController`.
- Require `chat_page_runtime.cpp` to register `ChatPageRuntimeFacade`, not `s_ui_controller`.
- Updated legacy burn-down register remaining callers for delivery and key verification bridges.

## Remaining Work

| Work | Direction |
| --- | --- |
| Conversation cache burn-down | Move remaining conversation list state to presentation workspace |
| Team rich payload rendering | Add Team rich payload projection outside controller |
| Team position picker renderer | Extract picker LVGL helper after action ownership is stable |
| App-wide scheduling | Move page event pump cadence into a broader runtime scheduler when product shell is ready |
