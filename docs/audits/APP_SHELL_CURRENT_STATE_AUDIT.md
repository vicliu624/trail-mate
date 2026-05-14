# App Shell Current State Audit

## Purpose

Phase 8.3 names the current app-shell surfaces before any source movement.

This audit separates each current `apps/` directory into app shell role, build
entrypoint leakage, platform part, board-specific part, future app shell
direction, and migration risk.

Core rule:

```text
Build Entrypoint invokes.
App Shell composes.
Target chooses.
Board describes.
UX Pack presents.
```

## Current App Directory Matrix

| Current path | Current role | App shell part | Build entrypoint part | Platform part | Board-specific part | Future app shell direction | Migration risk |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `apps/esp_idf` | ESP-IDF build entrypoint plus ESP runtime compatibility shell | startup runtime, loop runtime, app facade access, target runtime config | ESP-IDF CMake, component manifest, sdkconfig target descriptors, local build output | ESP-IDF adapter calls and radio/GPS/team runtime glue | target descriptors for Tab5 and T-Display-P4 variants | `apps/esp32_lvgl` invoked by `builds/esp_idf` | ESP-IDF include paths, component registration, generated build dirs, target sdkconfig defaults |
| `apps/esp_pio` | PlatformIO/Arduino compatibility entrypoint | Arduino startup, loop runtime, app runtime access | root `platformio.ini`, library layout, PIO environment assumptions | Arduino/PIO startup and compatibility access | legacy target coupling through current PIO environments | `apps/nrf52_node` plus possible legacy ESP PIO compatibility path | PIO environments couple ESP/nRF assumptions; root build must stay stable |
| `apps/linux_sim` | Linux simulator and developer-tooling shell | simulator target startup, simulator composition root, smoke/probe tests | app-local CMake, presets, simulator scripts | SDL/desktop platform integration and host tooling | simulator geometry/input assumptions | `apps/linux_sim_shell` invoked by `builds/linux_cmake` | CMake helper and scripts are app-local; simulator remains fast validation path |
| `apps/linux_uconsole` | Linux uConsole/AIO2-class handheld shell | uConsole composition root and app model | local GTK/CMake assumptions | GTK/Linux handheld integration | uConsole/AIO2 product assumptions | `apps/linux_uconsole_gtk` invoked by `builds/linux_cmake` | GTK shell and app model are still adjacent; do not destabilize runtime |
| `apps/linux_rpi` | Pi OS / Cardputer Zero device shell and bring-up path | Raspberry Pi/Cardputer Zero product startup | CMake framebuffer path and M5 SDK/SCons path | Linux framebuffer, evdev, SDK host integration | Cardputer Zero hardware/device assumptions | future Linux device app shell under `builds/linux_cmake` | hardware validation and dual build path must stay intact |
| `apps/linux_unoq` | future UNO Q Linux shell placeholder | target placeholder | not yet authoritative | future Linux device integration | UNO Q product assumptions | future Linux UNO Q app shell under `builds/linux_cmake` | target profile and build path are early |
| `apps/gat562_mesh_evb_pro` | nRF/mono device app shell with board-adjacent runtime | device startup, loop runtime, protocol factory, UI runtime | PIO/root build assumptions outside this directory | embedded runtime glue | direct `boards/gat562_mesh_evb_pro` access | `apps/nrf52_node` invoked by `builds/pio_nrf52` | board facts and app runtime are close; separate only after nRF wrapper parity |

## Findings

`apps/esp_idf` and `apps/esp_pio` are still historical app-shell illusions
because their names are build-system names and their responsibilities mix build
entrypoint, compatibility app shell, and runtime glue.

`apps/linux_sim`, `apps/linux_uconsole`, `apps/linux_rpi`, and
`apps/linux_unoq` are closer to app shells, but their build scripts and app
shell code are not yet normalized behind `builds/linux_cmake`.

`apps/gat562_mesh_evb_pro` has real product startup meaning, but it is too
board-specific to be the final generic nRF52 app shell name.

## Phase 8.3 Skeleton Direction

Phase 8.3 adds semantic skeletons:

| Future app shell | Current compatibility source | Build entrypoint | Renderer family | Status |
| --- | --- | --- | --- | --- |
| `apps/esp32_lvgl` | `apps/esp_idf` | `builds/esp_idf` | LVGL | skeleton only |
| `apps/nrf52_node` | `apps/esp_pio`, `apps/gat562_mesh_evb_pro` | `builds/pio_nrf52` | mono/status or future embedded UI | skeleton only |
| `apps/linux_uconsole_gtk` | `apps/linux_uconsole` | `builds/linux_cmake` | GTK | skeleton only |
| `apps/linux_sim_shell` | `apps/linux_sim` | `builds/linux_cmake` | simulator/ASCII/host UI | skeleton only |

## App Shell Boundary

An app shell may:

- select target profile
- select board package
- select platform family
- select UX profile / UX pack
- hand off to product composition
- start runtime facade

An app shell must not:

- own build host files
- define board facts
- contain SDK/HAL implementation
- implement screen internals
- assemble Chat/Map/GPS details behind hidden globals

## Migration Notes

The skeleton directories weaken the old assumption that `apps/esp_idf` and
`apps/esp_pio` are final app shell names. They do not change builds.

Future phases should migrate one shell at a time only after:

- build wrapper parity exists
- target profile documents identify the selected app shell
- UX profile documents identify the selected UX pack
- legacy compatibility adapters have explicit exit conditions
- existing build/test gates remain green

## Non-Action

Phase 8.3 does not move source files, change include paths, edit CMake,
edit PlatformIO, split `ui_shared`, implement UX packs, or create real app
composition roots.
