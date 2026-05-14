# UI Shared Split Audit

## Purpose

Phase 8 will eventually split `modules/ui_shared` into clearer UI, runtime, and
adapter modules. This audit classifies the current contents and assigns target
placement without moving files in Phase 8.1.

The classification is intentionally architectural. It does not claim every file
can move independently, and it does not require an immediate module split.

## Current Module Role

`modules/ui_shared` currently acts as a compatibility umbrella for embedded LVGL
UI code, page renderers, screen runtimes, presentation adapters, action bridges,
map helpers, assets, widgets, and tests.

This was useful while Phase 5-7 extracted presentation and runtime ownership.
It is now too broad to express the boundaries Phase 8 needs.

## Target Buckets

Expected target placements:

```text
ui_lvgl_core
  LVGL technical primitives

ui_lvgl_ux_packs
  device-specific pages, renderer variants, modals, pickers, input bindings

ui_legacy_adapters
  bounded compatibility adapters and bridges

ui_map_runtime
  map tile, overlay, render queue, and map runtime helpers

ui_chat_runtime
  chat page event pump, chat-specific runtime helpers

ui_gps_runtime
  GPS page runtime pump and GPS scheduling helpers
```

These names are target responsibilities for Phase 8 planning. They do not need
to be created in Phase 8.1.

## Classification Table

| Current content | Examples | Target placement | Notes |
| --- | --- | --- | --- |
| LVGL core widgets | `components/*`, `widgets/*`, `page/page_host.h`, `ui_theme.h`, top bar, busy overlay, toast | `ui_lvgl_core` | Technical UI primitives shared by UX packs |
| LVGL screens | `screens/chat`, `screens/gps`, `screens/team`, `screens/tracker`, `screens/settings`, `screens/node_info`, `screens/walkie_talkie` | `ui_lvgl_ux_packs` | Screen membership and feature depth should become UX pack choices |
| LVGL menu/navigation surfaces | `menu/*`, `app_catalog*`, `app_runtime`, `loop_shell`, `startup_ui_shell` | split between `ui_lvgl_core` and `ui_lvgl_ux_packs` | Primitive navigation host belongs to core; product menu membership belongs to UX pack |
| assets and fonts | `assets/*`, image/icon C files, font files | `ui_lvgl_core` or UX pack asset bundle | Common primitives can stay core; pack-specific icons should follow UX pack |
| legacy presentation sources | `presentation_sources/legacy_*_source.*`, `legacy_map_presentation_source`, `legacy_gps_status_source` | `ui_legacy_adapters` | Phase 7 bounded these; Phase 8 decides rename/delete/stable adapter |
| legacy action bridges | `legacy_chat_action_sink`, `legacy_chat_delivery_action_bridge`, `legacy_team_action_bridge`, `legacy_key_verification_action_sink` | `ui_legacy_adapters` | Adapter names should be normalized only after ownership replacement is clear |
| chat screen runtime helpers | `chat_page_runtime_event_pump`, `chat_ui_refresh_sink`, chat runtime proxy, chat UI runtime helpers | `ui_chat_runtime` | Runtime scheduling helpers should stay outside renderer/widget ownership |
| chat LVGL renderers/controllers | chat screen widgets, chat controller, key verification modal renderer | `ui_lvgl_ux_packs` with possible `ui_chat_runtime` split | UX pack chooses full/compact chat and modal implementation |
| team picker renderer | `team_position_picker_renderer.*` | `ui_lvgl_ux_packs` | It is LVGL-specific picker lifecycle, not Team action ownership |
| key verification modal renderer | `key_verification_modal_renderer.*` | `ui_lvgl_ux_packs` | Modal renderer belongs to UX pack/modal layer; model/source stay portable |
| team rich payload projection | `team_presentation/team_rich_payload_*` | portable presentation or `ui_chat_runtime` boundary | Projector is not LVGL; future placement should avoid renderer coupling |
| map tile helpers | `map_tiles/map_tile_types`, resolver, source, cache, render queue | `ui_map_runtime` | Tile source/cache/queue are map runtime helpers, not LVGL widgets |
| map overlay helpers | `map_overlay/map_overlay_projector` | `ui_map_runtime` or portable map presentation support | Keep overlay DTOs in `ui_presentation`; projector may live in runtime/adapters |
| map widgets | `widgets/map/*` | `ui_lvgl_core` if primitive, `ui_lvgl_ux_packs` if product workflow | Split technical tile drawing from device UX map workflows |
| GPS page runtime pump | `gps_page_runtime_pump.*`, `gps_ui_refresh_sink.h` | `ui_gps_runtime` | Runtime cadence owner; renderer remains passive |
| GPS/GNSS screens | `screens/gps`, `screens/gnss` | `ui_lvgl_ux_packs` plus `ui_gps_runtime` helpers | Full GPS pages may differ by UX pack |
| runtime memory/profile helpers | `runtime/memory_profile.*` | `ui_lvgl_core` or runtime support module | Placement depends on whether it is UI technical support or target runtime fact |
| support filesystem helpers | `support/lvgl_fs_utils.h` | LVGL/platform adapter boundary | Must not leak into `ui_presentation`; filesystem belongs adapter side |

## Required Categories

Phase 8 migration planning must keep these categories distinct:

- LVGL core widgets
- LVGL screens
- legacy presentation sources
- legacy action bridges
- chat screen runtime helpers
- map tile helpers
- team picker renderer
- key verification modal renderer
- GPS page runtime pump

They currently coexist in `ui_shared`; later movement should not collapse them
into a different single bucket.

## Split Rules

`ui_lvgl_core` may contain:

- LVGL themes
- primitive widgets
- technical navigation host
- input host primitives
- screen host primitives
- reusable visual components

`ui_lvgl_core` must not contain:

- target UX feature decisions
- Team action policy
- Chat service access
- map tile path/source ownership
- GPS runtime polling

`ui_lvgl_ux_packs` may contain:

- page sets
- renderer variants
- modal variants
- picker variants
- input binding sets
- layout profiles
- feature-depth decisions

`ui_lvgl_ux_packs` must not contain:

- board pin facts
- SDK/HAL calls
- build-system entrypoints
- protocol interpretation
- direct filesystem tile paths

`ui_legacy_adapters` may contain:

- compatibility bridges with explicit ports
- legacy source/sink adapters
- bounded anti-corruption objects

`ui_legacy_adapters` must not become:

- a dumping ground for new behavior
- an alternative app composition root
- a place where renderers pull runtime sources

`ui_map_runtime` may contain:

- map tile source/cache ports
- tile resolver
- render queue
- overlay source adapters
- map runtime helpers

`ui_chat_runtime` may contain:

- chat page event pump
- chat UI refresh sink
- chat runtime scheduling helpers

`ui_gps_runtime` may contain:

- GPS page runtime pump
- GPS UI refresh sink
- GPS runtime scheduling adapters

## Legacy Naming Pass

Phase 8 should classify each `Legacy*` object before renaming:

| Classification | Meaning | Action |
| --- | --- | --- |
| Temporary bridge | exists only to strangle old caller/source | keep `Legacy*` until removal path is active |
| Stable adapter | official adapter to a concrete source | rename to non-legacy adapter |
| Dead legacy | no longer used by target paths | delete after checker confirms no callers |

Examples to classify:

- `LegacyFilesystemMapTileSource`
- `LegacyMapOverlaySource`
- `LegacyTeamActionBridge`
- `LegacyChatDeliveryEventBridge`
- `LegacyKeyVerificationSource`

## Non-Goals

Phase 8.1 does not:

- create any target module directories
- move files out of `modules/ui_shared`
- change include paths
- rename classes
- alter build manifests
- change UI behavior

## Exit Condition For This Audit

This audit is satisfied when later Phase 8 work can point each `ui_shared`
change to one of the target buckets above and explain whether it is moving a
technical primitive, UX pack decision, runtime helper, map runtime helper, chat
runtime helper, GPS runtime helper, or legacy adapter.

## Phase 8 Structural Migration Batch

The first structural migration batch moves the Phase 7 extracted, lowest-risk
helpers out of `ui_shared` while keeping forwarding headers at the old include
paths.

Moved to `ui_chat_runtime`:

- `ChatPageRuntimeEventPump`
- `IChatUiRefreshSink`

Moved to `ui_gps_runtime`:

- `GpsPageRuntimePump`
- `IGpsUiRefreshSink`

Moved to `ui_map_runtime`:

- map tile types/source/cache/decoder-cache ports
- `MapTileResolver`
- `MapTileRenderQueue`
- `FilesystemMapTileSource`
- `MapOverlayProjector`

Moved to `ui_legacy_adapters`:

- `LegacyChatDeliveryEventBridge`
- `LegacyChatDeliveryActionBridge`
- `LegacyKeyVerificationSource`
- `LegacyKeyVerificationActionSink`
- `LegacyMapOverlaySource`

Moved to `ui_lvgl_ux_packs`:

- `TeamPositionPickerRenderer`
- key verification modal renderer

`LegacyFilesystemMapTileSource` is now a compatibility alias for
`FilesystemMapTileSource`. New code should include
`ui_map_runtime/map_tiles/filesystem_map_tile_source.h`.

## Phase 8 Structural Consolidation Policy

`ui_shared` is a transitional umbrella after the structural migration batch. The
compatibility rules are defined in
`docs/audits/UI_SHARED_COMPATIBILITY_SHIM_POLICY.md`.

Forwarding headers under `modules/ui_shared/include` are allowed only as
compatibility shims. Main code must include the authoritative new module paths:

- `ui_chat_runtime/...`
- `ui_map_runtime/...`
- `ui_gps_runtime/...`
- `ui_legacy_adapters/...`
- `ui_lvgl_core/...`
- `ui_lvgl_ux_packs/...`

New runtime helpers, legacy adapters, map tile helpers, GPS runtime helpers,
chat runtime helpers, and LVGL UX pack renderers must not be added to
`modules/ui_shared`.
