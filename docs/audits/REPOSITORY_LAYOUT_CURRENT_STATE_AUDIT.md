# Repository Layout Current State Audit

## Purpose

Phase 8 starts by naming the current repository layout accurately.

This audit records where current folder names already match architecture
semantics and where they are historical build, platform, or UI migration
artifacts. It does not require immediate movement.

## Current Top-Level Shape

Observed top-level architecture directories:

```text
apps/
boards/
docs/
modules/
platform/
tools/
```

Observed historical/build artifacts also exist at the top level:

```text
build/
build.cardputer/
build.tab5/
build.t_display_p4/
build.t_display_p4_amoled/
build.t_display_p4_tft/
cmake/
managed_components/
packs/
src/
third_party/
variants/
```

These may remain for current build compatibility. Phase 8 should not treat
their current placement as final architecture semantics without a migration
decision.

## Current `apps/`

Observed app/build shell directories:

```text
apps/
  esp_idf/
  esp_pio/
  gat562_mesh_evb_pro/
  linux_rpi/
  linux_sim/
  linux_uconsole/
  linux_unoq/
```

Audit finding:

`legacy/app_implementations/esp_idf` and
`legacy/app_implementations/esp_pio` are build entrypoints, not final app shells.

They are named after build systems and currently carry build-host concerns.
Phase 8 should classify them as transitional build entrypoints until a
`builds/esp_idf` and nRF/PIO entrypoint plan exists.

`legacy/app_implementations/linux_sim` and
`legacy/app_implementations/linux_uconsole` are historical implementation
roots. Their old names mixed shell, target, platform, and device identity; they
are no longer current `apps/` entries.

## Current `modules/`

Observed reusable modules include:

```text
modules/chat_presentation_adapters/
modules/core_chat/
modules/core_device/
modules/core_gps/
modules/core_hostlink/
modules/core_mesh/
modules/core_phone/
modules/core_sys/
modules/core_team/
modules/product_composition/
modules/ui_mono_128x64/
modules/ui_presentation/
modules/ui_shared/
```

Audit finding:

`modules/` is the right place for reusable code, but module names do not yet
separate all UI responsibilities. `ui_shared` currently mixes LVGL core,
widgets, screens, runtime bridges, presentation sources, action bridges, map
tile helpers, and screen-specific renderers.

## Current `platform/`

Observed platform directories:

```text
platform/
  esp/
  linux/
  nrf52/
  shared/
```

Audit finding:

The high-level platform split is recognizable. The remaining risk is platform
code carrying product behavior or app graph decisions while Phase 8 transitions
build entrypoints and app shells.

Platform code should provide SDK/HAL/runtime adapters. It should not decide UX
pack, page set, Chat/Map/GPS runtime ownership, or product feature depth.

## Current `boards/`

Observed board directories and manifests include:

```text
boards/
  gat562_mesh_evb_pro/
  tab5/
  tdeck/
  tdeck_pro/
  tlora_pager/
  twatchs3/
  t_display_p4/
  *.json
```

Audit finding:

Board facts are present and should remain descriptive. A board may describe
display size, touch support, keys, buses, GPS, radio, battery, and storage. It
must not decide whether the product exposes a full map, compact map, full chat,
quick chat, or status-only UX.

## Current UI Split Problem

`ui_shared` mixes LVGL core/widgets/screens/runtime bridges/adapters.

Current examples include:

- LVGL components and widgets under `include/ui/components`, `include/ui/widgets`
- screen implementations under `include/ui/screens` and `src/ui/screens`
- page/menu runtime helpers under `include/ui/page`, `include/ui/menu`, and
  `src/ui/menu`
- legacy presentation sources under `include/ui/presentation_sources`
- legacy action bridges under `include/ui/team_actions` and presentation source
  paths
- map tile helpers under `include/ui/map_tiles` and `src/ui/map_tiles`
- map overlay helpers under `include/ui/map_overlay` and `src/ui/map_overlay`
- chat screen runtime helpers under `include/ui/screens/chat`
- GPS page runtime pump under `include/ui/screens/gps`

This does not mean all code is wrongly placed. It means the current module name
no longer expresses the distinct responsibilities Phase 7 created.

## Device UX Modeling Gap

Device-specific UX differences are not yet modeled as UX packs.

In checker terms, current device-specific UX is still implicit rather than
declared by `DeviceUxProfile` and `UxPack` documents.

ESP/nRF UI appears in modules while uConsole is in apps. That means device
differences are currently represented partly by source placement, partly by app
shell, partly by build entrypoint, and partly by renderer implementation.

The missing architecture object is the UX pack:

```text
target product choice
  -> device UX profile
    -> UX pack
      -> screen set + input binding set + layout profile + renderer variants
```

Without UX packs, device behavior can drift into pages, board manifests,
platform adapters, or build-system directories.

## Legacy Adapter State

Legacy* adapters remain bounded but not renamed/deleted.

Examples recorded by Phase 7 include:

- `LegacyChatDeliveryEventBridge`
- `LegacyChatDeliveryActionBridge`
- `LegacyTeamActionBridge`
- `LegacyKeyVerificationSource`
- `LegacyKeyVerificationActionSink`
- `LegacyFilesystemMapTileSource`
- `LegacyMapOverlaySource`

Phase 7 contained these surfaces behind ports and reports. Phase 8 should sort
them into:

- temporary bridge to burn down
- stable official adapter to rename
- dead historical code to remove

## Main Current-State Findings

| Surface | Current state | Phase 8 interpretation |
| --- | --- | --- |
| `apps/esp_idf` | build-system named app directory | transitional build entrypoint |
| `apps/esp_pio` | build-system named app directory | transitional build entrypoint |
| Linux app dirs | app shell and target identity mixed | transitional product app shells |
| `modules/ui_shared` | mixed LVGL, screens, runtime helpers, adapters | split planning target |
| `platform/` | platform families visible | keep SDK/HAL/runtime adapter meaning strict |
| `boards/` | board facts visible | keep descriptive, not product deciding |
| Device UX | implicit in pages/apps/builds | introduce UX pack architecture |
| `Legacy*` adapters | contained, not normalized | rename/delete planning target |

## Explicit Non-Action For 8.1

Phase 8.1 should not:

- move `apps/esp_idf`
- move `apps/esp_pio`
- create the final `builds/` tree
- split `ui_shared`
- rename `Legacy*` classes
- rewrite CMake, PlatformIO, or ESP-IDF entrypoints
- change target startup behavior

The immediate action is to add repository layout and UX pack specifications plus
a gentle checker so later physical movement has a stable boundary to obey.
