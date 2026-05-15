# Key Verification / Map Overlay Legacy Burn-down Audit

## Purpose

Phase 9.5 burns down the remaining key verification and map overlay legacy
presentation adapters from main runtime ownership. The goal is not to move these
surfaces under `legacy/`; the goal is to make stable runtime modules the real
owners and leave old `Legacy*` headers as deprecated compatibility aliases only.

Audit fields intentionally include current callers, main runtime callers, test callers,
docs references, replacement, and migration decision for each legacy surface.

## LegacyKeyVerificationSession

| Field | Value |
| --- | --- |
| Current compatibility header | `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_session.h` |
| Former compatibility header | `modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_session.h` |
| Current implementation source | none; deprecated alias only |
| Former implementation source | session state was embedded in the legacy key verification adapter implementation |
| Current callers | deprecated alias headers and `test_legacy_key_verification_adapters_legacy_alias.cpp` only |
| Main runtime callers | none |
| Test callers | legacy alias smoke only |
| Docs references | historical Phase 7/8 audits and this burn-down audit |
| Replacement | `ui_key_verification_runtime::KeyVerificationSessionAdapter` |
| Migration decision | main runtime owns the stable session adapter name directly; old headers forward to it with `[[deprecated]]` |

## LegacyKeyVerificationSource

| Field | Value |
| --- | --- |
| Current compatibility header | `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_source.h` |
| Former compatibility header | `modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_source.h` |
| Current implementation source | `modules/ui_key_verification_runtime/src/key_verification_presentation_source.cpp` |
| Former implementation source | `modules/ui_legacy_adapters/src/legacy_key_verification_source.cpp` |
| Current callers | main runtime includes `ui_key_verification_runtime/key_verification_presentation_source.h`; legacy alias headers/tests only use the old name |
| Main runtime callers | `modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp` and `modules/ui_chat_runtime/src/chat_page_runtime_event_pump.cpp` use `KeyVerificationPresentationSource` |
| Test callers | `modules/ui_key_verification_runtime/tests/test_key_verification_runtime_adapters.cpp`; legacy alias smoke remains for compatibility |
| Docs references | historical Phase 7/8 audits and this burn-down audit |
| Replacement | `ui_key_verification_runtime::KeyVerificationPresentationSource` implementing `IKeyVerificationPresentationSource` |
| Migration decision | event pump and chat runtime consume the stable presentation source; old header is a deprecated alias |

## LegacyKeyVerificationActionSink

| Field | Value |
| --- | --- |
| Current compatibility header | `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_action_sink.h` |
| Former compatibility header | `modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_action_sink.h` |
| Current implementation source | `modules/ui_key_verification_runtime/src/key_verification_action_sink.cpp` |
| Former implementation source | `modules/ui_legacy_adapters/src/legacy_key_verification_action_sink.cpp` |
| Current callers | main runtime includes `ui_key_verification_runtime/key_verification_action_sink.h`; legacy alias headers/tests only use the old name |
| Main runtime callers | `modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp` uses `KeyVerificationActionSink` |
| Test callers | `modules/ui_key_verification_runtime/tests/test_key_verification_runtime_adapters.cpp`; legacy alias smoke remains for compatibility |
| Docs references | historical Phase 7/8 audits and this burn-down audit |
| Replacement | `ui_key_verification_runtime::KeyVerificationActionSink` implementing `IKeyVerificationActionSink` |
| Migration decision | key verification actions go through the stable command sink; old header is a deprecated alias |

## LegacyMapOverlaySource

| Field | Value |
| --- | --- |
| Current compatibility header | `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_map_overlay_source.h` |
| Former compatibility header | `modules/ui_shared/include/ui/presentation_sources/legacy_map_overlay_source.h` |
| Current implementation source | `modules/ui_map_runtime/src/map_overlay_snapshot_source.cpp` |
| Former implementation source | `modules/ui_legacy_adapters/src/legacy_map_overlay_source.cpp` |
| Current callers | main runtime includes `ui_map_runtime/map_overlay_snapshot_source.h`; legacy alias headers/tests only use the old name |
| Main runtime callers | `modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp` uses `MapOverlaySnapshotSource` |
| Test callers | `modules/ui_map_runtime/tests/test_map_overlay_snapshot_source.cpp`; legacy alias smoke remains for compatibility |
| Docs references | historical Phase 7/8 audits and this burn-down audit |
| Replacement | `ui::map_overlay::MapOverlaySnapshotSource` and `ui::map_overlay::MapOverlayProjectionAdapter` |
| Migration decision | GPS/map runtime consumes the stable snapshot source; old header is a deprecated alias |

## Caller Classification

Main runtime callers were migrated to stable owners:

- `chat_page_runtime.cpp` uses `KeyVerificationSessionAdapter`, `KeyVerificationPresentationSource`, and `KeyVerificationActionSink`.
- `chat_page_runtime_event_pump.*` uses `KeyVerificationPresentationSource`.
- `gps_page_runtime.cpp` uses `MapOverlaySnapshotSource`.

Compatibility callers are intentionally limited:

- `test_legacy_key_verification_adapters_legacy_alias.cpp`
- `test_legacy_map_overlay_source_legacy_alias.cpp`
- deprecated alias headers under `modules/ui_legacy_adapters/include`
- deprecated alias headers under `modules/ui_shared/include/ui/presentation_sources`

## Burn-down Decision

The old `LegacyKeyVerification*` and `LegacyMapOverlaySource` names are no
longer runtime owners. They remain only as compatibility aliases so downstream
includes fail softly while the main runtime graph uses stable module names.

## Checker Decision

`check_phase9_legacy_burndown_ready.py` must reject:

- main-code includes of the old key verification and map overlay legacy headers
- old key verification or map overlay legacy implementation sources in build lists
- legacy headers that contain real implementation logic

It may allow:

- deprecated forwarding aliases
- legacy alias smoke tests
- historical docs references
