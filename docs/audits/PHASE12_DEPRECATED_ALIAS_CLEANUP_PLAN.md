# Phase 12 Deprecated Alias Cleanup Plan

## Purpose

Phase 9 burned selected `Legacy*` surfaces down to deprecated aliases. Phase 12
does not remove those aliases immediately; it defines the deletion window.

## Alias Ledger

| Alias | Alias header path | Replacement header | Remaining includes | Delete now? | Deletion condition |
| --- | --- | --- | --- | --- | --- |
| `LegacyChatDeliveryActionBridge` | `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_chat_delivery_action_bridge.h` and `modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_action_bridge.h` | `ui_chat_runtime/chat_delivery_action_port_adapter.h` | compatibility alias tests; docs references | No | no downstream includes of either alias header remain |
| `LegacyChatDeliveryEventBridge` | `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_chat_delivery_event_bridge.h` and `modules/ui_shared/include/ui/presentation_sources/legacy_chat_delivery_event_bridge.h` | `ui_chat_runtime/chat_delivery_event_projection_adapter.h` | compatibility alias tests; docs references | No | no downstream includes of either alias header remain |
| `LegacyKeyVerificationSource` | `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_source.h` and `modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_source.h` | `ui_key_verification_runtime/key_verification_presentation_source.h` | compatibility alias tests; docs references | No | no downstream includes of either alias header remain |
| `LegacyKeyVerificationActionSink` | `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_action_sink.h` and `modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_action_sink.h` | `ui_key_verification_runtime/key_verification_action_sink.h` | compatibility alias tests; docs references | No | no downstream includes of either alias header remain |
| `LegacyKeyVerificationSession` | `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_key_verification_session.h` and `modules/ui_shared/include/ui/presentation_sources/legacy_key_verification_session.h` | `ui_key_verification_runtime/key_verification_session_adapter.h` | compatibility alias tests; docs references | No | no downstream includes of either alias header remain |
| `LegacyMapOverlaySource` | `modules/ui_legacy_adapters/include/ui_legacy_adapters/legacy_map_overlay_source.h` and `modules/ui_shared/include/ui/presentation_sources/legacy_map_overlay_source.h` | `ui_map_runtime/map_overlay_snapshot_source.h` | compatibility alias tests; docs references | No | no downstream includes of either alias header remain and map renderers consume stable snapshots only |

## Rules

- Main code must not include deprecated alias headers.
- Compatibility tests may include deprecated alias headers.
- Docs may mention historical alias names.
- Alias headers must remain forwarding-only while they exist.
- New `Legacy*` bridge names are forbidden for new runtime work.

## Cleanup Window

Alias deletion is safe only after a repo-wide and downstream include search
shows no remaining production include paths. Until then, the aliases remain
compatibility shims with checker coverage.
