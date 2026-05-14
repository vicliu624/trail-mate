# Chat Runtime Event Pump Audit

## Purpose

Phase 7.7 moves Chat runtime scheduling and event projection out of `ChatUiController`.

The controller may refresh UI, open modals, and navigate screens. It must not own runtime event projection or ChatService scheduling.

## Current State Before Phase 7.7

| Runtime concern | Current owner before 7.7 | Problem | Target owner |
| --- | --- | --- | --- |
| EventBus chat event entry | `IChatUiRuntime::onChatEvent(...)` registered to `ChatUiController` | controller receives runtime events directly | `ChatPageRuntimeFacade` / `ChatPageRuntimeEventPump` |
| `ChatSendResultEvent` delivery projection | `ChatUiController::onChatEvent(...)` calls `LegacyChatDeliveryEventBridge` | UI controller updates delivery runtime projection | `ChatPageRuntimeEventPump` |
| Key verification source/session update | `ChatUiController::onChatEvent(...)` calls `LegacyKeyVerificationSource` | UI controller updates runtime/presentation session | `ChatPageRuntimeEventPump` |
| `ChatService::processIncoming()` | `ChatUiController::update()` | UI tick schedules runtime work | `ChatPageRuntimeEventPump::update()` |
| `ChatService::flushStore()` | `ChatUiController::update()` | UI tick owns store flush cadence | `ChatPageRuntimeEventPump::update()` |
| UI refresh after new message | `ChatUiController::onChatEvent(...)` | valid UI refresh mixed with runtime projection | `IChatUiRefreshSink::onRuntimeMessageArrived(...)` |
| UI refresh after send result | `ChatUiController::onChatEvent(...)` | valid UI refresh mixed with delivery projection | `IChatUiRefreshSink::onRuntimeSendResult(...)` |
| Key verification modal display | `ChatUiController::onChatEvent(...)` | valid modal display mixed with source/session update | `IChatUiRefreshSink::showKeyVerification(...)` |

## Phase 7.7 Split

`ChatUiController::onChatEvent(...)` currently performs both:

- runtime projection
- UI refresh

`ChatUiController::update()` currently performs both:

- `ChatService::processIncoming()` / `ChatService::flushStore()`
- UI refresh dirty-check

Phase 7.7 splits these responsibilities:

| Responsibility | Owner |
| --- | --- |
| Runtime event routing | `ChatPageRuntimeEventPump` |
| Delivery projection | `LegacyChatDeliveryEventBridge` called by event pump |
| Key verification source/session update | `LegacyKeyVerificationSource` called by event pump |
| Chat incoming/store cadence | `ChatPageRuntimeEventPump::update()` |
| UI list/conversation refresh | `IChatUiRefreshSink` implemented by `ChatUiController` |
| Runtime facade registration | `chat_page_runtime.cpp` registers `ChatPageRuntimeFacade`, not the controller |

## Remaining Legacy

| Legacy surface | Why it remains | Exit condition |
| --- | --- | --- |
| `ChatPageRuntimeEventPump` calls `ChatService` directly | It is the Phase 7.7 runtime pump owner | Later app shell scheduler can own cadence across all pages |
| `ChatUiController` still reads `ChatService` for UI refresh | Conversation cache and legacy screen components still use core `ChatMessage` | Chat presentation workspace fully drives conversation UI |
| Key verification events still use legacy source/session | Protocol-specific key verification adapters remain legacy | Split or rename protocol adapters |
| Team rich payload formatting remains in controller | Not part of event-pump ownership | Team rich payload presentation adapter exists |
