# Chat UI Controller Burn-down Audit

## Purpose

Phase 7.6 records what `ChatUiController` is still allowed to do, what runtime ownership has already moved out, and what legacy responsibilities remain.

This audit prevents new Chat / Team / key verification ownership from being added back to the legacy controller.

## A. Temporary UI Responsibilities

| Responsibility | Why it may remain | Boundary |
| --- | --- | --- |
| LVGL screen lifecycle | Current chat screen is still an LVGL controller/shell | May create and destroy screen widgets only |
| Modal open / close coordination | Existing key verification and Team position picker are modal UI flows | Must render from snapshots or submit action DTOs |
| Textarea input parsing | Compose and verification number inputs are widget concerns | Must not encode runtime payloads or access protocol APIs |
| Button callback dispatch | LVGL callbacks still enter the controller | Callback must submit to model/action sink, not own runtime state |
| Conversation screen navigation | Existing chat UI state machine remains controller-local | Must not define delivery/key/team ownership |
| Team position picker workflow coordination | Controller starts picker workflow and handles selected/cancel results | Widget lifecycle belongs to `TeamPositionPickerRenderer`; send path submits `TeamActionRequest` |
| IME attach / detach | Existing LVGL input integration remains widget lifecycle | Does not affect Chat/Team runtime state |

## B. Migrated Runtime Responsibilities

| Migrated responsibility | New owner | Controller rule |
| --- | --- | --- |
| Delivery read state | `ChatDeliveryReadModel` owned by runtime/composition | Controller must not own or clear the read model |
| Delivery event projection | `ChatDeliveryEventProjector` behind `IChatDeliveryEventPort` | Controller may only forward legacy events while event pump is legacy |
| Delivery actions | `ChatDeliveryActionService` / `IChatDeliveryActionSink` | Controller must not directly retry/cancel/clear delivery records |
| Team location/command payload encoding | `LegacyTeamActionBridge` / Team action runtime adapter | Controller submits `TeamActionRequest` only |
| Team rich payload display projection | `TeamRichPayloadProjector` / `TeamChatPresentationSource` | Controller consumes `team_chat_model_.snapshot()` rows only |
| Key verification state/session | `KeyVerificationModel` plus `LegacyKeyVerificationSource` | Controller selects peer and renders snapshot only |
| Key verification submit/trust runtime calls | `LegacyKeyVerificationActionSink` | Controller calls `KeyVerificationModel` actions only |
| Key verification modal rendering | `KeyVerificationModalRenderer` helper | Controller owns open/close and callback forwarding only |
| Team position picker widget lifecycle | `TeamPositionPickerRenderer` | Controller owns selected/cancel workflow only |

## C. Remaining Legacy Responsibilities

| Remaining responsibility | Current caller | Exit condition | Status |
| --- | --- | --- | --- |
| Conversation cache | `cached_conversations_` and list refresh helpers | Presentation workspace provides all conversation list projection needed by controller | remaining legacy |
| `ChatService::processIncoming` | `ChatPageRuntimeEventPump::update()` | App-wide runtime scheduler owns cadence outside page runtime | burned down from controller |
| `ChatService::flushStore` | `ChatPageRuntimeEventPump::update()` | App-wide runtime scheduler owns cadence outside page runtime | burned down from controller |
| EventBus forwarding | `ChatPageRuntimeFacade` / `ChatPageRuntimeEventPump` | App-wide runtime scheduler owns EventBus routing outside page runtime | burned down from controller |
| Team position picker selection workflow | `onTeamPositionIconSelected(...)` / `onTeamPositionCancel(...)` | Future workflow coordinator or UX pack-specific picker flow owns selection routing | contained |
| Legacy Team log formatting outside Chat UI | Contacts/GPS/team-page legacy paths | Shared Team presentation projection is reused by those screens | remaining legacy |
| Protocol mismatch UX | Reply guard notifications | Protocol support model exposes UI-ready disabled reason | remaining legacy |

## Forbidden Additions

`ChatUiController` must not add new code that:

- encodes Team location or command payloads
- calls key verification runtime APIs directly
- owns `ChatDeliveryReadModel`, `ChatDeliveryEventProjector`, or `ChatDeliveryActionService`
- adds key verification fields to `ChatWorkspaceModel` or `MessageRow`
- maps Team to DirectPeer or Channel conversation semantics
- decodes or formats Team location/command payloads for chat display
- creates or stores Team position picker overlay, panel, hint label, group, or icon event contexts
