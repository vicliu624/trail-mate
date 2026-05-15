# Repository Layout Architecture Specification

## Purpose

Phase 8 normalizes Trail Mate repository semantics before any directory move.

This specification defines what each top-level architecture directory means,
what dependency directions are allowed, and which historical shapes must remain
transitional until they can be renamed or moved safely.

Phase 8.1 is documentation and checker work only. It must not move build
entrypoints, change PlatformIO or ESP-IDF layout, split `ui_shared`, or rename
legacy adapters.

## Core Rule

Repository folders name architecture responsibilities, not incidental build
systems.

```text
Build Entrypoint invokes.
App Shell composes.
Target chooses.
Board describes.
Platform adapts.
Module provides.
UX Pack presents.
Renderer draws.
```

## Directory Semantics

| Directory | Meaning | Owns | Must not own |
| --- | --- | --- | --- |
| `apps/` | Product app shell / target app shell | product startup shell, target composition consumer, UX pack selection, lifecycle handoff | build-system identity, SDK project scaffolding, board hardware facts, reusable module logic |
| `builds/` | Build system entrypoints | thin wrappers for ESP-IDF, PlatformIO, CMake, and future build hosts | product behavior, app service graphs, runtime object construction, UX decisions |
| `modules/` | Reusable modules | portable cores, presentation models, runtime helpers, renderers, adapters with explicit ports | build entrypoints, app shell startup, board-specific product selection |
| `platform/` | OS / SDK / HAL / runtime adapters | ESP, nRF, Linux, and shared platform integration, SDK/HAL bindings, driver hosts | product behavior, UX feature decisions, app shell composition |
| `boards/` | Board hardware facts | pins, buses, displays, storage devices, power rails, electrical capabilities | product features, UI page sets, protocol policy, app services |
| `docs/targets/` | Target product selection documentation | target profiles, selected board/platform/app shell/UX pack/capability intent | runtime configuration sprawl, generated build state |
| `docs/ux_profiles/` | Device UX profile documents | device-specific UX declarations, feature-set decisions, screen sets, input bindings | board facts, SDK details, renderer internals |

The current tree may not yet match these names. Phase 8 uses this document to
define the target semantics before file movement starts.

## Dependency Direction

Allowed direction:

```text
builds -> apps
apps -> modules + platform + boards
modules -> modules only
platform -> SDK/HAL/runtime only
boards -> facts only
docs -> specification/audit only
```

Expanded reading:

- `builds/` may invoke an app shell entrypoint and provide build-system glue.
- `apps/` may select modules, platform adapters, board packages, and UX packs.
- `modules/` may depend on other modules through declared public contracts.
- `platform/` may depend on SDK, HAL, OS, and runtime host APIs.
- `boards/` may provide static facts and manifests only.
- `docs/` may describe specifications, audits, target decisions, and UX profile
  decisions.

Forbidden direction:

```text
modules -> builds
modules -> apps
platform -> apps
boards -> apps
ui_presentation -> platform
ui_presentation -> LVGL
ui_presentation -> filesystem
```

These directions are forbidden because they make portable code depend on
entrypoints, product shells, hardware packages, concrete UI toolkits, or storage
hosts.

## App Shells

An app shell represents a product target's composition-facing startup surface.

Examples of future app shell names:

```text
apps/esp32_lvgl/
apps/nrf52_node/
apps/linux_uconsole_gtk/
apps/linux_sim/
apps/linux_headless/
```

An app shell may:

- select a target profile
- select a board package
- select a UX pack
- connect product composition
- connect platform adapters
- start the app runtime
- hand off to the renderer shell

An app shell must not:

- be named only after a build system such as `esp_idf` or `esp_pio`
- own ESP-IDF CMake, PlatformIO, or Linux CMake project mechanics
- implement HAL details
- decide board facts
- implement screen internals

`apps cannot be named by build system.`

## Build Entrypoints

A build entrypoint is a thin wrapper around the authoritative build host for a
platform family.

Target direction:

```text
builds/
  esp_idf/
  pio_nrf52/
  linux_cmake/
```

Expected authorities:

| Platform family | Authoritative build entrypoint |
| --- | --- |
| ESP / ESP32-P4 | ESP-IDF |
| nRF52 | PlatformIO |
| Linux | CMake |

A build entrypoint may:

- expose the build-system project expected by ESP-IDF, PlatformIO, or CMake
- include minimal wrapper files needed by the build host
- call the selected app shell startup function
- pass build flags and generated manifests to app composition

A build entrypoint must not:

- create business object graphs
- assemble Chat, Map, Team, or GPS runtime services
- decide UX pack or feature availability
- hold product-specific page behavior

`builds cannot carry product app shell.`

## Modules

Modules are reusable units. They may be portable domain cores, presentation
contracts, runtime helper modules, UI toolkit primitives, or explicit adapters.

Module names should express responsibility, not current migration history.

Expected Phase 8 target split examples:

```text
modules/ui_presentation/
modules/ui_lvgl_core/
modules/ui_lvgl_ux_packs/
modules/ui_legacy_adapters/
modules/ui_map_runtime/
modules/ui_chat_runtime/
modules/ui_gps_runtime/
```

Portable presentation modules must remain UI-toolkit independent. In
particular, `ui_presentation` must not depend on LVGL, GTK, platform headers, or
filesystem APIs.

`modules cannot depend on builds.`

## Platform

`platform/` contains concrete SDK, OS, HAL, runtime, driver-host, and storage
adapter code.

Platform code may:

- integrate ESP-IDF, Arduino compatibility, nRF SDK/PIO, Linux, GTK, SDL, or
  filesystem hosts
- provide platform factories
- adapt timers, queues, storage, input, display, GPS, radio, BLE, and network
  capabilities

Platform code must not:

- decide product feature sets
- own business policy
- create app services behind hidden globals
- decide device UX

`platform cannot carry product behavior.`

## Boards

`boards/` describes hardware facts.

Board facts include:

- chip and module identity
- pins and buses
- display dimensions and panel facts
- input devices
- storage devices
- radio and GPS peripherals
- power and battery facts

Board facts must not select product features. A board may say that a display is
320x240 and has touch. It must not decide whether the target has a full map
workflow, compact map workflow, team chat, or status-only UX.

`boards cannot decide UX.`

## Target Documents

`docs/targets/` records target product selections.

A target document may say:

- which board package is selected
- which platform family is selected
- which app shell is selected
- which UX profile or UX pack is selected
- which capabilities are enabled, disabled, proxied, or hidden

It must not become an unbounded runtime configuration system.

## UX Profile Documents

`docs/ux_profiles/` records device UX declarations.

A UX profile document may say:

- which screen set exists
- which input bindings are active
- which features are full, compact, hidden, or status-only
- which renderer family or UX pack is used
- which layout profile is selected

It must not restate board pin facts or build-system mechanics.

## Transitional Historical Layout

Current `legacy/app_implementations/esp_idf` and
`legacy/app_implementations/esp_pio` are transitional build entrypoint shapes.
They may remain while the build entrypoint migration is planned, but Phase 8
must treat them as legacy build entrypoint directories, not final app shell
names.

Current `modules/ui_shared` is a transitional mixed UI module. It may remain in
place while the split is planned, but new Phase 8 work must classify additions
as LVGL core, UX pack, runtime helper, map runtime, chat runtime, GPS runtime,
or legacy adapter.

## Phase 8.2 Progress

Phase 8.2 establishes builds/ skeleton but does not move build host files.

The intended authoritative build entrypoint families are:

```text
ESP / ESP32-P4 -> builds/esp_idf
nRF52 -> builds/pio_nrf52
Linux -> builds/linux_cmake
```

`legacy/app_implementations/esp_idf` and `legacy/app_implementations/esp_pio` remain transitional
until wrapper migration.

`builds/esp_idf`, `builds/pio_nrf52`, and `builds/linux_cmake` are thin wrapper
slots. They must not assemble Chat, Map, Team, or GPS runtime, choose UX packs,
define board facts, or become product app shells.

Phase 8.2 does not change ESP-IDF CMake, PlatformIO, Linux CMake, app runtime
behavior, or existing target startup paths.

## Phase 8.3 Progress

Phase 8.3 establishes app shell skeletons but does not move runtime
implementations or build host files.

Skeleton app shells:

```text
apps/esp32_lvgl
apps/nrf52_node
apps/linux_uconsole_gtk
apps/linux_sim_shell
```

These names describe product/runtime shape rather than build systems. They are
architecture anchors for future migration. They do not build or alter runtime
behavior in Phase 8.3.

Target profile documentation under `docs/targets/` maps product target names to
board, platform family, build entrypoint, app shell, UX profile placeholder, and
status.

The first structural migration batch also creates the target UI modules:

```text
modules/ui_chat_runtime
modules/ui_map_runtime
modules/ui_gps_runtime
modules/ui_legacy_adapters
modules/ui_lvgl_core
modules/ui_lvgl_ux_packs
```

`modules/ui_shared` remains a transitional umbrella, but the moved Phase 7
runtime/helper/adapter files must not gain new implementation at their old
paths. Old headers may remain as forwarding headers only.

## Non-Goals

Phase 8.1 does not:

- move `apps/esp_idf`
- move `apps/esp_pio`
- create `builds/`
- split `modules/ui_shared`
- rename any `Legacy*` adapter
- change ESP-IDF, PlatformIO, or CMake files
- change app runtime behavior
- change UI screens

## Review Rules

New architecture work should answer:

- Is this an app shell, build entrypoint, platform adapter, board fact, module,
  UX pack, or documentation artifact?
- Does the folder name match that responsibility?
- Does the dependency direction follow the allowed direction?
- Is a historical directory being treated as canonical by accident?
- Is a board fact being promoted into a UX decision?
- Is a build-system wrapper being promoted into a product app shell?

Phase 8 file movement may start only after these distinctions are preserved by
specification, audit, and checker coverage.
