# Build Entrypoint Normalization Audit

## Purpose

Phase 8.2 establishes build entrypoint semantics without moving the current
build systems.

The audit distinguishes:

- build entrypoint
- transitional build entrypoint
- product app shell
- platform adapter
- board facts

The normalization baseline is:

```text
ESP / ESP32-P4 authoritative build entrypoint = builds/esp_idf
nRF52 authoritative build entrypoint = builds/pio_nrf52
Linux authoritative build entrypoint = builds/linux_cmake
```

Core rule:

```text
Build Entrypoint invokes.
App Shell composes.
```

## Current Build/App Surface

| Current path | Current role | Final role | Migration risk | Blocking reason | Next action |
| --- | --- | --- | --- | --- | --- |
| `legacy/app_implementations/esp_idf` | ESP-IDF build entrypoint plus transitional ESP app/runtime shell | `builds/esp_idf` thin wrapper invoking a future ESP LVGL app shell | ESP-IDF CMake include paths, target sdkconfig defaults, generated build dirs, component registration | Existing ESP-IDF targets build through this directory | Create wrapper skeleton first; move build-host files only after equivalent invocation is proven |
| `legacy/app_implementations/esp_pio` | PlatformIO/Arduino compatibility entrypoint and legacy ESP app glue | split into `builds/pio_nrf52` for nRF52 authority and possible legacy ESP PIO compatibility wrapper | PlatformIO environments and root `platformio.ini` still reference current layout | nRF and legacy ESP build flows are coupled through PIO conventions | Keep transitional; introduce family-specific wrapper docs before moving files |
| `legacy/app_implementations/linux_sim` | Linux simulator and developer-tooling shell with CMake project files | Linux app shell invoked by `builds/linux_cmake` | CMake source helper and simulator scripts live in current app directory | Simulator remains a fast validation target and should not be disrupted | Document as transitional CMake app shell; wrapper should invoke existing CMake path first |
| `legacy/app_implementations/linux_uconsole` | uConsole/AIO2-class Linux handheld shell | Linux app shell invoked by `builds/linux_cmake` | GTK/uConsole app model and platform code are adjacent | Device shell is still evolving and tied to app-local structure | Keep app shell; route future canonical build through Linux CMake wrapper |
| `legacy/app_implementations/linux_rpi` | Pi OS / Cardputer Zero device shell and CMake/SCons bring-up paths | Linux app shell invoked by `builds/linux_cmake` | CMake framebuffer path and M5 SDK/SCons path coexist | Device path still needs build and hardware validation | Keep as transitional Linux app shell; wrapper should not absorb device behavior |
| `legacy/app_implementations/linux_unoq` | Future UNO Q Linux shell placeholder | Linux app shell invoked by `builds/linux_cmake` | Early target, low source volume | Final product identity and build path not fully proven | Keep as app shell candidate; delay migration until target profile exists |
| `legacy/app_implementations/gat562_mesh_evb_pro` | nRF/mono device app shell with board-specific runtime access | future nRF52 app shell invoked by `builds/pio_nrf52` | Board app shell and board facts are currently close together | PIO/nRF split is not yet represented by final app shell names | Keep stable; later separate app shell from board facts and PIO wrapper |
| root `platformio.ini` | current PlatformIO build authority for Arduino/PIO targets | invoked or wrapped by `builds/pio_nrf52` and any legacy PIO compatibility entrypoint | Environment names, library paths, and board configs are sensitive | Current PIO build must remain unbroken | Do not edit in Phase 8.2; document as existing authority |
| root `CMakeLists.txt` | root CMake/ESP-IDF historical entrypoint surface | clarified by `builds/esp_idf` and `builds/linux_cmake` wrappers | Root CMake has ESP-IDF history and Linux CMake must not hijack it | Multiple CMake host meanings coexist | Do not edit in Phase 8.2; document target authority before moving |
| `legacy/app_implementations/esp_idf/build.tab5` | generated or local ESP-IDF build output under app tree | build artifact outside final source semantics | Generated files may be large or host-specific | Existing developer workflow produced it there | Do not normalize in Phase 8.2; clean artifact policy separately |
| root `build/` | generated build output | build artifact | May contain local host output | Unknown local use | Leave untouched |
| root `build.cardputer` | generated target build output | build artifact | May be referenced by local tooling | Unknown local use | Leave untouched |
| root `build.tab5` | generated target build output | build artifact | May be referenced by local tooling | Unknown local use | Leave untouched |
| root `build.t_display_p4` | generated target build output | build artifact | May be referenced by local tooling | Unknown local use | Leave untouched |
| root `build.t_display_p4_amoled` | generated target build output | build artifact | May be referenced by local tooling | Unknown local use | Leave untouched |
| root `build.t_display_p4_tft` | generated target build output | build artifact | May be referenced by local tooling | Unknown local use | Leave untouched |

## Findings

`legacy/app_implementations/esp_idf` is not a final product app shell. It is a transitional ESP-IDF
build entrypoint and compatibility shell.

`legacy/app_implementations/esp_pio` is not a final product app shell. It is a transitional
PlatformIO/Arduino build entrypoint and compatibility shell.

Linux app directories are closer to app shells, but they still carry local
CMake or device bring-up mechanics. Phase 8.2 should document that future
`builds/linux_cmake` wrappers invoke selected Linux app shells rather than
absorbing their product behavior.

Generated `build*` directories are not architecture directories. They should
not be treated as app shells, build entrypoint source trees, modules, boards, or
platform adapters.

## BuildEntrypoint Constraints

A build entrypoint may:

- hold build-system wrapper files
- call an app shell entrypoint
- pass build flags, target identity, and generated paths
- isolate ESP-IDF, PlatformIO, or CMake conventions

A build entrypoint must not:

- assemble Chat runtime
- assemble Map runtime
- assemble GPS runtime
- select UX pack
- define board facts
- interpret protocol payloads
- own renderer widget trees

## Transitional Compatibility Rule

`legacy/app_implementations/esp_idf` and `legacy/app_implementations/esp_pio` are allowed to remain because they are
recorded transitional historical build entrypoints.

New build-system-named directories under `apps/` should not be added. Future
build-system identity belongs under `builds/`.

## Phase 8.2 Scope

Phase 8.2 creates only:

- `docs/decisions/ADR_BUILD_ENTRYPOINTS.md`
- `docs/audits/BUILD_ENTRYPOINT_NORMALIZATION_AUDIT.md`
- `docs/audits/TRANSITIONAL_BUILD_ENTRYPOINTS.md`
- `builds/README.md`
- `builds/esp_idf/README.md`
- `builds/pio_nrf52/README.md`
- `builds/linux_cmake/README.md`

No build behavior changes in this phase.

## Phase 8 Correction: Linux Executable Wrapper Baseline

Phase 8 Correction changes the Linux line from declared-only authority to an
executable wrapper baseline.

`builds/linux_cmake` now contains a real `CMakeLists.txt`. It is still a thin
build wrapper: it does not include `TrailMateLinuxSources.cmake`, does not call
`trailmate_add_linux_common`, and does not assemble Chat/Map/GPS services. Its
job is to invoke app shells through `add_subdirectory`.

Executable proof chain:

```text
builds/linux_cmake
  -> apps/linux_uconsole_gtk
  -> apps/linux_sim_shell
```

Linux app shell status:

| App shell | Executable target | Transitional source | Status |
| --- | --- | --- | --- |
| `apps/linux_uconsole_gtk` | `trailmate_linux_uconsole_gtk_shell` | `legacy/app_implementations/linux_uconsole` | executable app shell baseline |
| `apps/linux_sim_shell` | `trailmate_linux_sim_shell` | `legacy/app_implementations/linux_sim` | executable app shell baseline |

The old implementation roots are explicitly marked:

- `legacy/app_implementations/linux_uconsole/TRANSITIONAL_IMPLEMENTATION_ROOT.md`
- `legacy/app_implementations/linux_sim/TRANSITIONAL_IMPLEMENTATION_ROOT.md`

ESP-IDF and PlatformIO physical migration remains deferred. The Linux line is
the first proof of the direction:

```text
Build Entrypoint invokes.
App Shell composes.
```

## Phase 8 Build/AppShell Executable Convergence

Phase 8 Build/AppShell Executable Convergence extends the executable baseline
to all three platform families without moving physical build files.

Executable proof chains:

```text
builds/linux_cmake
  -> apps/linux_uconsole_gtk
  -> linux_uconsole_gtk_historical_source_descriptor
  -> product_composition / modules/ui_gtk_runtime

builds/linux_cmake
  -> apps/linux_sim_shell
  -> linux_sim_historical_source_descriptor
  -> product_composition / modules/ui_ascii_runtime

builds/esp_idf
  -> apps/esp32_lvgl
  -> trailmate_esp32_lvgl_app_shell
  -> builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake
  -> apps/esp32_lvgl + platform/esp/radio

builds/pio_nrf52
  -> apps/nrf52_node
  -> trailmate-nrf52-node-app-shell
  -> trailmate_nrf52_pio_legacy_adapter
  -> legacy/app_implementations/esp_pio / legacy/app_implementations/gat562_mesh_evb_pro
```

The wrapper files are intentionally thin:

- `builds/linux_cmake/CMakeLists.txt` invokes Linux app shells.
- `builds/esp_idf/CMakeLists.txt` invokes the ESP32 LVGL app shell baseline.
- `builds/pio_nrf52/platformio.ini` declares the nRF52 PlatformIO wrapper
  authority and transitional roots.

The following historical roots are explicitly transitional implementation
roots:

- `legacy/app_implementations/linux_uconsole`
- `legacy/app_implementations/linux_sim`
- `legacy/app_implementations/esp_idf`
- `legacy/app_implementations/esp_pio`
- `legacy/app_implementations/gat562_mesh_evb_pro`

Physical migration remains deferred for ESP-IDF sdkconfig/component registration
and current PlatformIO environments. This batch proves the authority direction:

```text
Build Entrypoint invokes.
App Shell selects.
Transitional Adapter delegates.
Legacy implementation still runs.
```
