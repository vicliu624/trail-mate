# Target UI Final Ownership Report

Batch 3 completed final ownership metadata for all currently requested target
IDs without moving old UI files into a new wrapper layer.

Batch 4 removed root `legacy/`. Historical legacy source roots are recorded
only in `docs/archive/REMOVED_LEGACY_ROOTS.md`; no source archive is retained.

## Coverage

Covered target IDs:

- `tab5`
- `tdisplayp4_tft`
- `tdisplayp4_amoled`
- `tlora_pager`
- `tdeck`
- `twatch`
- `uconsole`
- `cardputerzero`
- `gat562_mesh_evb_pro`

## Final Ownership Map

| responsibility | final owner | Batch 3 artifact |
| --- | --- | --- |
| target detection and product route | `modules/product_composition` | `TargetProfile` |
| build route binding | `modules/product_composition` | `TargetBuildBinding` |
| desired/current UX binding | `modules/product_composition` | `TargetUxBinding` |
| display and input facts | `boards/*` plus UI profile metadata | `BOARD.md`, `board_facts.h`, `TargetUiProfile` |
| page set | `modules/ui_presentation` | `PageManifest` |
| layout decision | `modules/ui_presentation` | `LayoutProfile` |
| LVGL consumption | `modules/ui_lvgl_ux_packs` | existing descriptor/pack runtime |
| GTK consumption | `modules/ui_gtk_runtime` | existing descriptor page registry path |
| ASCII consumption | `modules/ui_ascii_runtime` | existing descriptor renderer path |
| headless consumption | `modules/ui_headless_runtime` | existing headless descriptor consumer from Batch 1 |
| app wiring | `apps/*` | app shell `targetProfile()`, `targetId()`, `activeUxPackId()` |
| build wiring | `builds/*` | `builds/esp_idf`, `builds/linux_cmake`, `builds/pio_nrf52` |

## UI Profile Decisions

| target family | UI profile | evidence boundary |
| --- | --- | --- |
| `tab5` | `tab5_touch_ui` | board profile proves touch/display/audio/SD/GPS/LoRa; dimensions remain repo-evidence pending |
| `tdisplayp4` | `tdisplayp4_touch_ui` | board profile proves touch plus HI8561/RM69A10 panel facts |
| `tlora_pager` | `pager_compact_ui` | board YAML and variant env prove compact landscape build size and keyboard/rotary input |
| `tdeck` | `deck_wide_ui` | board YAML proves keyboard/touch/trackball and 320 x 240 display |
| `twatch` | `watch_compact_ui` | board code and variant env prove 240 x 240 touch watch route |
| `uconsole` | `uconsole_desktop_ui` | Linux uConsole capability doc proves GTK desktop keyboard/pointer route |
| `cardputerzero` | `cardputer_compact_ui` | Linux docs and common display profile prove current 320 x 170 Linux shell route |
| `gat562_mesh_evb_pro` | `node_headless_ui` | nRF52 capability doc says product UI host is none; board facts still retain the physical 128 x 64 display |

## Page Set Ownership

Page membership now lives in `modules/ui_presentation/page/page_manifest.*`.
The manifests use page IDs already present in the repository's existing shared
screen and UX documentation. The headless node manifest contains only
node-status and diagnostics surfaces and does not receive the full GUI page
set.

## Layout Ownership

Layout shape now lives in `modules/ui_presentation/layout/layout_profile.*`.
Profiles for large touch, compact pager, deck, watch, desktop, Cardputer, and
headless node are separate final DTOs. They are not widget implementations and
do not create LVGL or GTK objects.

## App Shell Wiring

App shells now expose:

- `targetId()`
- `targetProfile()`
- `activeUxPackId()`

`activeUxPackId()` resolves through `TargetUxBinding`, so fallback selection is
not hidden inside the shell.

## Explicit Non-Introductions

- No intermediate UI layer introduced.
- No transitional UI adapter introduced.
- No legacy UI extraction layer introduced.
- No old UI file was copied wholesale into a new module.
- No renderer is allowed to choose a target or UX pack.
- No board fact file is allowed to choose product presentation.
- No active architecture layer may be named `legacy/`.

## Remaining Gaps

The following gaps are real and intentionally recorded instead of guessed:

- final executable UX packs for `tab5_touch`, `tdisplayp4_touch`,
  `pager_compact`, `deck_full`, `watch_compact`, `cardputer_compact`, and
  `node_headless`;
- ESP-IDF target defaults for `tlora_pager`, `tdeck`, and `twatch`;
- dedicated Cardputer Zero app shell decision.
