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
| `LegacyChatDeliveryEventBridge` | `ChatDeliveryEventProjectionAdapter` / `IChatDeliveryEventPort` / `ChatDeliveryEventProjector` | no main runtime callers; deprecated alias headers and legacy alias tests only | remove alias headers after downstream includes of `LegacyChatDeliveryEventBridge` are gone | 9.4 | burned-down to deprecated alias |
| `LegacyChatDeliveryActionBridge` | `ChatDeliveryActionPortAdapter` / `IChatDeliveryActionPort` | no main runtime callers; deprecated alias headers and legacy alias tests only | remove alias headers after downstream includes of `LegacyChatDeliveryActionBridge` are gone | 9.4 | burned-down to deprecated alias |
| `LegacyTeamActionBridge` | `TeamActionRequest` / `ITeamActionSink` / Team runtime command port | `ChatUiController::sendTeamLocationWithIcon(...)` submits `LocationMarker` intent; `chat_page_runtime.cpp` owns bridge lifetime | `TeamActionService` replaces the legacy bridge and renderers/controllers only submit `TeamActionRequest` | 7.x | contained |
| `LegacyKeyVerificationSource` | `KeyVerificationPresentationSource` / `IKeyVerificationPresentationSource` in `modules/ui_key_verification_runtime` | no main runtime callers; deprecated alias headers and legacy alias tests only | remove alias headers after downstream includes of `LegacyKeyVerificationSource` are gone | 9.5 | burned-down to deprecated alias |
| `LegacyKeyVerificationActionSink` | `KeyVerificationActionSink` / `IKeyVerificationActionSink` in `modules/ui_key_verification_runtime` | no main runtime callers; deprecated alias headers and legacy alias tests only | remove alias headers after downstream includes of `LegacyKeyVerificationActionSink` are gone | 9.5 | burned-down to deprecated alias |
| `ChatUiController` key verification modal rendering | `KeyVerificationModalRenderer` helper consumes `KeyVerificationSnapshot` | `ChatUiController` opens/closes modal and forwards submit/trust callbacks | Full modal view object owns widget lifecycle and controller keeps only workflow routing | 7.x | burned-down |
| `ChatUiController` Team payload encoding | `LegacyTeamActionBridge` | None expected for send path; controller submits `TeamActionRequest` only | Checker forbids `encodeTeamChatLocation` / `encodeTeamChatCommand` / raw `TeamChatMessage` send encoding in controller | 7.6 | burned-down |
| `ChatUiController` delivery mutation | `ChatDeliveryReadModel` / `ChatDeliveryActionService` | None expected; event pump forwards send-result events to delivery bridge | Checker forbids direct `ChatDeliveryReadModel`, `ChatDeliveryEventProjector`, `ChatDeliveryActionService`, and `LegacyChatDeliveryEventBridge` ownership in controller | 7.6 / 7.7 | burned-down |
| `ChatUiController` runtime event pump | `ChatPageRuntimeEventPump` / `ChatPageRuntimeFacade` | None expected; app facade registers runtime facade instead of controller | Checker forbids `ChatUiController::onChatEvent(...)`, `processIncoming()`, `flushStore()`, and key verification source projection calls in controller | 7.7 | burned-down |
| `ChatUiController` Team rich payload formatting | `TeamRichPayloadProjector` / `TeamChatPresentationSource` | None in `ChatUiController`; Team chat rows consume projected summaries from `team_chat_model_.snapshot()` | Checker forbids `format_team_chat_entry(...)`, `decodeTeamChatLocation(...)`, `decodeTeamChatCommand(...)`, `TeamChatLocation`, and `TeamChatCommand` in controller | 7.8 | burned-down |
| `ChatUiController` Team position picker renderer | `TeamPositionPickerRenderer` | `ChatUiController` calls open/close/updateHint and handles selected/cancel workflow only | UX pack-specific picker replaces shared LVGL renderer or renderer is renamed as official chat picker view | 7.9 | burned-down |
| `MessageRow` Team rich display limitations | `TeamRichPayloadDisplay` / Team row renderer | `TeamChatPresentationSource` projects rich payloads to summary text for current renderer compatibility | Rich Team row/card renderer consumes structured display fields directly | 7.x | contained |
| `Map tile path/cache legacy runtime` | `MapTileResolver` / `LegacyFilesystemMapTileSource` / map tile cache owner | Platform LVGL map tile runtimes call source/resolver; ESP decoded cache, Linux downloader cache, and uConsole path fields remain contained | Renderer consumes tile refs/source without direct path/cache ownership, and decoded/downloader caches are moved behind stable runtime adapters | 7.10 / 7.x | contained |
| `Map tile visible plan in platform renderer` | `MapTileRenderQueue` | Platform map tile runtimes still populate queue from legacy `MapTile` records | Dedicated map runtime computes visible queue and renderer consumes queue rows without mutating plan state | 7.11 / 7.x | contained |
| `ESP decoded LVGL tile cache` | `LvglDecodedTileCache` / `IMapTileDecoderCache` | ESP map tile runtime still asks the LVGL cache adapter for decoded handles | Runtime-owned decoder cache is injected into a dedicated map tile renderer | 7.11 / 7.x | contained |
| `Map overlay current/team marker projection` | `MapOverlaySnapshotSource` / `MapOverlayProjectionAdapter` / `MapOverlayProjector` in `modules/ui_map_runtime` | no main runtime callers of `LegacyMapOverlaySource`; deprecated alias headers and legacy alias tests only | remove alias headers after downstream includes of `LegacyMapOverlaySource` are gone, and map renderers consume `MapOverlaySnapshotSource` only | 9.5 | burned-down to deprecated alias |
| `Map route/tracker overlay projection` | deferred route/tracker presentation source | ESP route/tracker draw callbacks still own route/tracker widget drawing | Route/tracker stores expose presentation sources and renderer consumes overlay snapshot rows | 7.12 / 7.x | contained |
| `GPS page refresh cadence` | `GpsPageRuntimePump` / `IGpsUiRefreshSink` | ESP and Linux GPS page timers tick `GpsPageRuntimePump`; page-local adapters preserve legacy refresh behavior | Page-local adapters are replaced by GPS presentation refresh models and renderers consume snapshots only | 7.13 / 7.x | contained |

## Phase 9 Runtime Adoption Burn-down

| Legacy surface | New owner | Remaining callers | Removal condition | Target phase | Status |
| --- | --- | --- | --- | --- | --- |
| `legacy/app_implementations/linux_sim` ASCII descriptor adapters | `modules/ui_ascii_runtime` | moved out of legacy; legacy LinuxSim CMake links module-owned sources for compatibility smoke coverage | real simulator entry consumes `AsciiRuntimeScreenGraphPresenter` and old hardcoded routing is fallback-only | 9.2 | burned-down |
| `legacy/app_implementations/linux_uconsole` GTK descriptor adapters | `modules/ui_gtk_runtime` | moved out of legacy; legacy uConsole CMake links module-owned sources for compatibility smoke coverage | real GTK page switch path consumes `GtkUConsoleScreenGraphPresenter` and old hardcoded routing is fallback-only | 9.2 | burned-down |
| Runtime entry adoption helpers under `legacy/app_implementations` | `modules/ui_ascii_runtime`, `modules/ui_gtk_runtime`, `modules/ui_lvgl_ux_packs`, final app-shell probes | none; Phase 9.2 keeps entry adoption helpers out of legacy | checker forbids `*RuntimeEntryAdoption` files and tokens under `legacy/app_implementations` | 9.2 | burned-down |
| Runtime entry bridges under `legacy/app_implementations` | final app-shell runtime entry/page-registry adoption and `modules/ui_lvgl_ux_packs` | none; Phase 9.3 keeps real entry adoption out of legacy | checker forbids Phase 9 runtime adoption bridge files under `legacy/app_implementations` | 9.3 | burned-down |
| Chat delivery legacy bridge pair | `modules/ui_chat_runtime` formal ports and adapters | no main runtime callers; `ui_legacy_adapters` and `ui_shared` headers are deprecated aliases only | remove alias headers after downstream compatibility includes are gone | 9.4 | burned-down to deprecated alias |
| Key verification legacy source/sink/session | `modules/ui_key_verification_runtime` stable `KeyVerificationPresentationSource`, `KeyVerificationActionSink`, and `KeyVerificationSessionAdapter` | no main runtime callers; `ui_legacy_adapters` and `ui_shared` headers are deprecated aliases only | remove alias headers after downstream compatibility includes are gone | 9.5 | burned-down to deprecated alias |
| Map overlay legacy source | `modules/ui_map_runtime` stable snapshot source and projection adapter | no main runtime callers; `ui_legacy_adapters` and `ui_shared` headers are deprecated aliases only | remove alias headers after downstream compatibility includes are gone | 9.5 | burned-down to deprecated alias |

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
| Map tile path policy in viewport/renderer | forbidden |
| Map tile render queue boundary | required |
| ESP decoded tile cache owner | required |
| Map overlay source boundary | required |
| GPS runtime scheduling pump | required |
| Legacy bridges without removal condition | forbidden by register token check |
| Key verification stable adapter names | required by Phase 9.5 checker |
| Map overlay stable adapter names | required by Phase 9.5 checker |

## Phase 9.6 Final Readiness Alignment

Phase 9.6 does not add new legacy surfaces. It aligns the register with the
Phase 9 final readiness report:

- Chat delivery legacy bridge pair: main runtime callers removed; deprecated
  aliases only; stable owner is `ChatDeliveryActionPortAdapter` and
  `ChatDeliveryEventProjectionAdapter`.
- Key verification legacy source/sink/session: main runtime callers removed;
  deprecated aliases only; stable owner is `KeyVerificationPresentationSource`,
  `KeyVerificationActionSink`, and `KeyVerificationSessionAdapter`.
- Map overlay legacy source: main runtime callers removed; deprecated aliases
  only; stable owner is `MapOverlaySnapshotSource` and
  `MapOverlayProjectionAdapter`.

The remaining Phase 10-facing UI fallbacks are not legacy adapter burn-down items.
They are tracked in `docs/audits/PHASE9_FALLBACK_CONTAINMENT_LEDGER.md` as
contained fallback with owner and exit condition.
