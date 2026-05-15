# ADR: Build Entrypoint Authority

## Status

Accepted for Phase 8 baseline.

## Context

Trail Mate supports embedded ESP targets, nRF52 targets, and Linux targets.
Their build hosts are not interchangeable:

- ESP / ESP32-P4 targets need the ESP-IDF route.
- nRF52 targets still rely on PlatformIO for the current stable build flow.
- Linux targets use CMake for simulator and device shells.

Historically, some build-host identity leaked into `apps/` names. That made
`apps/esp_idf` and `apps/esp_pio` look like product app shells even though they
were build entrypoint and compatibility directories. Their current containment
paths are `legacy/app_implementations/esp_idf` and
`legacy/app_implementations/esp_pio`.

Phase 8 separates these concepts:

```text
Build Entrypoint invokes.
App Shell composes.
```

## Decision

The authoritative build entrypoint families are:

| Target family | Authoritative build entrypoint | Build host |
| --- | --- | --- |
| ESP / ESP32-P4 | `builds/esp_idf` | ESP-IDF |
| nRF52 | `builds/pio_nrf52` | PlatformIO |
| Linux | `builds/linux_cmake` | CMake |

`legacy/app_implementations/esp_idf and legacy/app_implementations/esp_pio are transitional historical build entrypoints.`
They are not final product app shells.

The final shape should move build-host wrappers toward `builds/` while product
startup and target composition live under app-shell directories such as:

```text
apps/esp32_lvgl/
apps/nrf52_node/
apps/linux_uconsole_gtk/
apps/linux_sim/
apps/linux_headless/
```

These final app shell names are examples of semantics, not a Phase 8.2
implementation requirement.

## Rationale

ESP / ESP32-P4 -> ESP-IDF because ESP32-P4 support and modern ESP targets are
best represented by ESP-IDF as the authoritative SDK and build host.

nRF52 -> PlatformIO because the current nRF-compatible build and board flow is
anchored in PlatformIO and should not be destabilized by Phase 8 directory
normalization.

Linux -> CMake because Linux simulator, uConsole, Raspberry Pi, UNO Q, and
headless shells already use CMake-oriented host builds and tooling.

Separating build entrypoints from app shells keeps build-system mechanics from
being mistaken for product architecture.

## Build Entrypoint Contract

A build entrypoint is a thin wrapper.

It may:

- provide build-host project files
- invoke the selected app shell
- pass target manifest or build flags to app composition
- adapt build-system conventions to repository paths

It must not:

- assemble Chat, Map, Team, or GPS runtime object graphs
- choose UX pack
- define board facts
- implement product behavior
- become a service locator

## App Shell Contract

An app shell is a composition root consumer.

It may:

- select target profile
- select board package
- select platform adapters
- select UX pack
- start app runtime
- hand off to renderer shell

It must not:

- carry ESP-IDF, PlatformIO, or CMake project mechanics as its identity
- own reusable modules
- define board facts
- hide build-host glue inside product logic

## BuildEntrypointManifest

Future build entrypoints may include a small `BuildEntrypointManifest` to
declare:

- build authority
- target family
- transitional source path
- final app shell it invokes
- migration status

The manifest is an architecture manifest. It does not decide UX, board facts,
Chat/Map/GPS runtime policy, or product feature depth.

## Transitional Paths

Current transitional paths:

- `apps/esp_idf`
- `apps/esp_pio`

These paths remain valid for compatibility until wrappers under `builds/` can
invoke final product app shells without breaking existing build flows.

## Consequences

Phase 8.2 creates `builds/` skeleton directories and documentation only.

It does not:

- move `apps/esp_idf`
- move `apps/esp_pio`
- modify ESP-IDF CMake files
- modify `platformio.ini`
- modify Linux CMake files
- create final app shell directories
- change runtime behavior

Later Phase 8 work may migrate one build family at a time once wrappers can be
validated against existing build gates.
