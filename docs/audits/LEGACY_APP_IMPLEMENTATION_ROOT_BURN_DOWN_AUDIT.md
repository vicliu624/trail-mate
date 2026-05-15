# Legacy App Implementation Root Burn-Down Audit

This audit starts the legacy root burn-down. It does not authorize deleting
the full `legacy/` tree. It separates historical implementation roots from the
post-refactor runtime, descriptor, and renderer ownership that now lives in
`apps/`, `modules/`, `platform/`, and `builds/`.

Burn-down rule: a legacy app implementation root may remain only as a
historical or transitional implementation root. It must not regain ownership
of `RuntimeEntryAdoption`, `DescriptorRenderer`, or `ScreenGraphPresenter`
logic.

## Required Root Coverage

- `legacy/app_implementations/esp_idf`
- `legacy/app_implementations/esp_pio`
- `legacy/app_implementations/gat562_mesh_evb_pro`
- `legacy/app_implementations/linux_rpi`
- `legacy/app_implementations/linux_sim`
- `legacy/app_implementations/linux_uconsole`
- `legacy/app_implementations/linux_unoq`

`linux_unoq` is included because it is present in the repository and already
appears in `legacy/app_implementations/LEGACY_IMPLEMENTATION_INDEX.md`.

## CMake And PlatformIO References Listed For Checker

Known build references that still touch legacy roots:

- `CMakeLists.txt` references `legacy/app_implementations/esp_idf`
- `builds/esp_idf/CMakeLists.txt` documents `legacy/app_implementations/esp_idf`
- `builds/pio_nrf52/platformio.ini` references `legacy/app_implementations/esp_pio`
- `builds/pio_nrf52/platformio.ini` references `legacy/app_implementations/gat562_mesh_evb_pro`
- `cmake/TrailMateLinuxSources.cmake` documents shared Linux source ownership
  for `legacy/app_implementations/linux_sim`,
  `legacy/app_implementations/linux_rpi`, and
  `legacy/app_implementations/linux_uconsole`
- `apps/esp32_lvgl/CMakeLists.txt` references `legacy/app_implementations/esp_idf`
- `apps/linux_sim_shell/CMakeLists.txt` references `legacy/app_implementations/linux_sim`
- `apps/linux_uconsole_gtk/CMakeLists.txt` references `legacy/app_implementations/linux_uconsole`
- `legacy/app_implementations/esp_idf/CMakeLists.txt` is the ESP-IDF historical component root
- `legacy/app_implementations/linux_sim/CMakeLists.txt` is the simulator historical CMake root
- `legacy/app_implementations/linux_uconsole/CMakeLists.txt` is the uConsole historical CMake root

## Root: `legacy/app_implementations/esp_idf`

- Current purpose: ESP-IDF historical component and target configuration root.
  It still owns ESP-IDF component registration, target descriptors, sdkconfig
  defaults, and compatibility adapter source for the `apps/esp32_lvgl` shell.
- Current build callers: root `CMakeLists.txt`, `builds/esp_idf/CMakeLists.txt`,
  ESP-IDF component registration under this root, and `apps/esp32_lvgl/CMakeLists.txt`.
- Current app shell callers: `apps/esp32_lvgl` uses the transitional source
  string and legacy implementation adapter.
- Current CMake / PlatformIO callers: root `CMakeLists.txt`,
  `builds/esp_idf/CMakeLists.txt`, and `apps/esp32_lvgl/CMakeLists.txt`.
- Current include callers: `apps/esp32_lvgl/CMakeLists.txt` adds the legacy
  adapter include directory for `esp_idf_legacy_implementation_adapter.h`.
- Runtime ownership status: transitional embedded build root; reusable runtime
  and presentation ownership must stay in modules or final app shells.
- Safe to delete now? no.
- First deletion blocker: ESP-IDF component registration and target sdkconfig
  defaults still physically live under this root.
- Target replacement owner: `apps/esp32_lvgl`, `builds/esp_idf`,
  `modules/ui_lvgl_ux_packs`, `modules/product_composition`, and ESP platform
  adapters.
- Burn-down step: transfer ESP-IDF build ownership into the authoritative build
  wrapper and final app shell only after target component registration can be
  invoked without this directory as a semantic app root.
- Deletion blocker: ESP-IDF component registration and target sdkconfig defaults
  still live in the historical root.
- Replacement owner: `apps/esp32_lvgl` plus `builds/esp_idf`.

## Root: `legacy/app_implementations/esp_pio`

- Current purpose: PlatformIO / Arduino compatibility root for nRF-oriented
  embedded builds and legacy PIO source layout.
- Current build callers: `builds/pio_nrf52/platformio.ini` references this
  root through include paths and compatibility source layout.
- Current app shell callers: `apps/nrf52_node` exposes the transitional source
  string `legacy/app_implementations/esp_pio`.
- Current CMake / PlatformIO callers: `builds/pio_nrf52/platformio.ini`.
- Current include callers: PlatformIO include flags still add
  `legacy/app_implementations/esp_pio/src`.
- Runtime ownership status: transitional PIO compatibility root; concrete device
  validation should use `gat562_mesh_evb_pro` as the board-specific target.
- Safe to delete now? no.
- First deletion blocker: PIO include/source layout still references this root.
- Target replacement owner: `apps/nrf52_node`, `builds/pio_nrf52`,
  `boards/gat562_mesh_evb_pro`, platform adapters, and embedded runtime modules.
- Burn-down step: split PIO compatibility glue from concrete device ownership,
  then make the PIO root forwarding-only or remove it after the wrapper owns
  all source and include paths.
- Deletion blocker: PlatformIO source and include paths still point at this
  historical root.
- Replacement owner: `apps/nrf52_node` plus `builds/pio_nrf52`.

## Root: `legacy/app_implementations/gat562_mesh_evb_pro`

- Current purpose: concrete GAT562 Mesh EVB Pro historical device root with
  board-adjacent runtime glue.
- Current build callers: `builds/pio_nrf52/platformio.ini` lists this root as
  part of the current PIO/nRF transitional layout.
- Current app shell callers: `apps/nrf52_node` exposes
  `legacy/app_implementations/gat562_mesh_evb_pro` as the board-specific
  transitional source.
- Current CMake / PlatformIO callers: `builds/pio_nrf52/platformio.ini`.
- Current include callers: no direct main-code header include is required by
  final app shells; remaining references are transitional source strings and
  build wrapper documentation.
- Runtime ownership status: concrete board historical root; board facts must
  remain in `boards/gat562_mesh_evb_pro` and app semantics in `apps/nrf52_node`.
- Safe to delete now? no.
- First deletion blocker: board-specific nRF/PIO bring-up still uses this root
  as a concrete device compatibility marker.
- Target replacement owner: `apps/nrf52_node`, `builds/pio_nrf52`,
  `boards/gat562_mesh_evb_pro`, and platform/embedded modules.
- Burn-down step: prove concrete GAT562 validation through the final nRF app
  shell and build wrapper, then remove this root as an app semantic boundary.
- Deletion blocker: concrete device compatibility marker is still referenced by
  the nRF app shell and PIO wrapper.
- Replacement owner: `apps/nrf52_node` plus `boards/gat562_mesh_evb_pro`.

## Root: `legacy/app_implementations/linux_rpi`

- Current purpose: historical Pi OS / Cardputer Zero Linux device bring-up root.
- Current build callers: `builds/linux_cmake/README.md` lists it as an
  app-local Linux path; no final app shell has taken this root over yet.
- Current app shell callers: no final Linux RPI app shell exists yet.
- Current CMake / PlatformIO callers: historical Linux CMake path and local root
  files under `legacy/app_implementations/linux_rpi`.
- Current include callers: no final app-shell direct include path is recorded in
  current primary app CMake.
- Runtime ownership status: historical Linux device bring-up root; not a
  post-refactor runtime adoption owner.
- Safe to delete now? no.
- First deletion blocker: Pi OS / Cardputer Zero app-shell replacement has not
  been created or validated.
- Target replacement owner: future Linux device app shell, `builds/linux_cmake`,
  `platform/linux`, and board/hardware boundaries.
- Burn-down step: create or select the final Linux device app shell, move build
  ownership there, and leave hardware facts behind platform/board boundaries.
- Deletion blocker: no final Linux RPI app shell owns this product path.
- Replacement owner: future Linux device app shell plus `platform/linux`.

## Root: `legacy/app_implementations/linux_sim`

- Current purpose: historical simulator implementation root with old simulator
  target files, local CMake, smoke tests, scripts, and compatibility adapter.
- Current build callers: `apps/linux_sim_shell/CMakeLists.txt`,
  `legacy/app_implementations/linux_sim/CMakeLists.txt`, and Linux build wrapper
  documentation in `builds/linux_cmake/README.md`.
- Current app shell callers: `apps/linux_sim_shell` uses the transitional source
  string and `linux_sim_legacy_implementation_adapter`.
- Current CMake / PlatformIO callers: `apps/linux_sim_shell/CMakeLists.txt` and
  `legacy/app_implementations/linux_sim/CMakeLists.txt`.
- Current include callers: `apps/linux_sim_shell/CMakeLists.txt` adds the legacy
  adapter include directory for `linux_sim_legacy_implementation_adapter.h`.
- Runtime ownership status: contained historical root. Phase 9-11 runtime entry
  adoption, ASCII descriptor rendering, and screen graph presentation are owned
  by `apps/linux_sim_shell`, `modules/ui_ascii_runtime`, and
  `modules/product_composition`.
- Safe to delete now? no.
- First deletion blocker: legacy simulator composition root, simulator local
  CMake target, adapter smoke coverage, and old simulator scripts still live
  under this root.
- Target replacement owner: `apps/linux_sim_shell`,
  `modules/ui_ascii_runtime`, `modules/product_composition`, and platform
  simulator support.
- Burn-down step: move or retire the historical simulator composition root and
  local CMake/script ownership after final app shell smoke coverage no longer
  needs the legacy adapter.
- Deletion blocker: legacy simulator composition root and local CMake/script
  ownership still exist.
- Replacement owner: `apps/linux_sim_shell` plus `modules/ui_ascii_runtime`.

## Root: `legacy/app_implementations/linux_uconsole`

- Current purpose: historical uConsole GTK implementation root with old GTK
  widget/page code, local CMake, smoke tests, and compatibility adapter.
- Current build callers: `apps/linux_uconsole_gtk/CMakeLists.txt`,
  `legacy/app_implementations/linux_uconsole/CMakeLists.txt`, and Linux build
  wrapper documentation in `builds/linux_cmake/README.md`.
- Current app shell callers: `apps/linux_uconsole_gtk` uses the transitional
  source string and `uconsole_legacy_implementation_adapter`.
- Current CMake / PlatformIO callers: `apps/linux_uconsole_gtk/CMakeLists.txt`
  and `legacy/app_implementations/linux_uconsole/CMakeLists.txt`.
- Current include callers: `apps/linux_uconsole_gtk/CMakeLists.txt` adds the
  legacy adapter include directory for `uconsole_legacy_implementation_adapter.h`.
- Runtime ownership status: contained historical GTK root. Phase 9-11 page
  registry adoption, GTK descriptor registry, and screen graph presentation are
  owned by `apps/linux_uconsole_gtk` and `modules/ui_gtk_runtime`.
- Safe to delete now? no.
- First deletion blocker: old GTK widget/page implementation, local CMake target,
  uConsole composition root, and adapter smoke coverage still live under this
  root.
- Target replacement owner: `apps/linux_uconsole_gtk`, `modules/ui_gtk_runtime`,
  `modules/product_composition`, and `platform/linux`.
- Burn-down step: move real GTK widget creation to consume descriptor page
  registry output, then retire the historical local CMake root or make it
  forwarding-only.
- Deletion blocker: old GTK widget/page implementation and local CMake target
  still live here.
- Replacement owner: `apps/linux_uconsole_gtk` plus `modules/ui_gtk_runtime`.

## Root: `legacy/app_implementations/linux_unoq`

- Current purpose: historical placeholder for a future UNO Q Linux shell.
- Current build callers: `builds/linux_cmake/README.md` lists it as a Linux
  app-local path; no final app shell currently owns it.
- Current app shell callers: none.
- Current CMake / PlatformIO callers: local transitional root files and Linux
  build wrapper documentation.
- Current include callers: none recorded in final app-shell source.
- Runtime ownership status: placeholder historical root; it must not own
  post-refactor runtime adoption or renderer descriptor logic.
- Safe to delete now? maybe, but not in this batch.
- First deletion blocker: product decision is still open: create a final UNO Q
  app shell or delete the placeholder as unused.
- Target replacement owner: future Linux UNO Q app shell or no replacement if
  the target is retired.
- Burn-down step: decide whether UNO Q is a real product target; then create the
  final app shell or delete the placeholder root.
- Deletion blocker: target ownership decision is not yet made.
- Replacement owner: future Linux UNO Q app shell or explicit retirement.

## Linux Root Burn-Down Baseline

### `legacy/app_implementations/linux_sim`

- Must remain for now: historical simulator implementation, local CMake, old
  simulator target, adapter smoke test, and compatibility scripts.
- Must not return here: runtime entry adoption, descriptor renderer, app shell,
  or screen graph presenter ownership.
- Candidate next move: retire or transfer the historical simulator composition
  root once `apps/linux_sim_shell` owns all simulator entry validation.
- Baseline status: no `RuntimeEntryAdoption`, `DescriptorRenderer`, or
  `ScreenGraphPresenter` ownership remains under the root.

### `legacy/app_implementations/linux_uconsole`

- Must remain for now: old GTK widget/page implementation, local CMake, adapter
  smoke test, and compatibility app target.
- Must not return here: page registry adoption, runtime descriptor presenter,
  screen graph bridge, or stable GTK descriptor registry ownership.
- Candidate next move: make real GTK widget creation consume descriptor page
  registry output, then retire historical page registry ownership.
- Baseline status: no `RuntimeEntryAdoption`, `DescriptorRenderer`, or
  `ScreenGraphPresenter` ownership remains under the root.

## Burn-Down Guardrail

The first burn-down checker should not require root deletion. It should require
this audit, replacement owners, deletion blockers, listed build references, and
no post-refactor runtime adoption or renderer descriptor tokens under
`legacy/app_implementations` source files.
