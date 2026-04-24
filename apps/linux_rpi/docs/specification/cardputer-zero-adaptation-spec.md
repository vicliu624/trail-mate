# Cardputer Zero Final-Shape Adaptation Specification

This document defines the target architectural shape for adapting
`Cardputer Zero` into the main `Trail Mate` repository.

It is not a feature checklist.
It is not a "first vertical slice" note.
It is the constraint document that later feature slices must obey.

## 1. Why this specification exists

The Linux work already has useful migration notes, simulator bring-up, and a Pi
OS device shell baseline.

That is not enough.

Without an explicit final-shape specification, a "first shippable slice" can
quietly become the place where structure is discovered by accident:

- the shortest feature path starts defining the layering
- platform glue leaks upward into shared modules
- temporary compatibility decisions become long-term boundaries
- a runnable page is mistaken for a valid architecture

This document prevents that.

## 2. What must be distinguished

### Final shape vs verification slice

- The final shape defines what the system is allowed to be.
- A verification slice is only allowed to test whether that shape is workable.
- A slice must never define the architecture retroactively.

### Shared core vs shared presentation

- `modules/core_*` are not the same kind of shared code as `modules/ui_shared`.
- Core modules own domain, protocol, policy, and use-case logic.
- `ui_shared` owns shared presentation composition and LVGL-based UI behavior.

If those are not distinguished, "shared" becomes meaningless and every layer
starts learning details it should never know.

### Platform contract vs platform implementation

- A runtime/storage/device capability contract is not the same thing as its
  Linux, ESP, or simulator implementation.
- Contracts must remain stable enough that both `linux_sim` and `linux_rpi`
  can cooperate through them without knowing each other's internals.

### App shell vs platform adapter

- `apps/*` compose startup and choose implementations.
- `platform/*` implements the contracts.

If those two are mixed, every app shell becomes another place where business
logic and platform details grow.

### Vendor SDK vs repository architecture

- `M5Stack_Linux_Libs` is a device-facing dependency.
- It is not the source of Trail Mate's architectural boundaries.
- The SDK should be used below our platform boundary, not define it.

## 3. Normative target layers

The Cardputer Zero adaptation should converge toward these layers.

### Layer A: Shared Core

Primary ownership:

- `modules/core_sys`
- `modules/core_chat`
- `modules/core_gps`
- `modules/core_team`
- `modules/core_hostlink`

Responsibilities:

- domain objects
- protocol models and codecs
- use cases
- policy/state logic
- serialization rules
- pure utility logic

Forbidden knowledge:

- `Arduino.h`
- ESP-IDF APIs
- FreeRTOS APIs
- LVGL types
- board pin maps
- Linux device paths
- SDL types
- fbdev / evdev / ioctl details
- vendor SDK types

Rule:

- A core module must be able to compile in a host-side test harness without any
  platform SDK or UI framework.

### Layer B: Shared Presentation

Primary ownership:

- `modules/ui_shared`

Responsibilities:

- shared menu shell
- screen composition
- shared assets
- shared formatting and presentation helpers
- LVGL object graphs and screen behavior that are portable across supported
  targets

Allowed knowledge:

- LVGL
- shared core modules
- platform contracts such as `platform::ui::*`

Forbidden knowledge:

- `platform/esp/...` include roots
- `Preferences`
- Arduino-only helpers
- Linux file paths or desktop simulator geometry
- board GPIO or driver internals
- vendor SDK types

Rule:

- `ui_shared` may depend on a UI framework.
- It may not depend on a platform implementation.

### Layer C: Platform Contracts

Primary ownership:

- `modules/core_sys/include/platform/ui/*`

Responsibilities:

- runtime capability interfaces
- persistence/storage ports
- transport/runtime capability ports
- stable value types needed by shared presentation

Examples:

- `device_runtime`
- `screen_runtime`
- `time_runtime`
- `settings_store`
- `wifi_runtime`
- `gps_runtime`
- `lora_runtime`
- `hostlink_runtime`

Rule:

- Contracts define collaboration.
- Contracts do not perform platform work.

### Layer D: Platform Implementations

Primary ownership:

- `platform/esp/*`
- `platform/linux/common`
- `platform/linux/rpi`

Responsibilities:

- implementing Layer C contracts
- OS/runtime integration
- hardware and SDK binding
- Linux/ESP storage adapters
- device-specific power, display, input, bus, and transport glue

Allowed knowledge:

- OS APIs
- SDKs
- driver headers
- Linux paths
- SDL, fbdev, evdev
- board-specific details

Forbidden ownership:

- domain rules
- screen composition policy
- app navigation
- long-lived business state that belongs in shared modules

### Layer E: App Shells

Primary ownership:

- `apps/esp_pio`
- `apps/esp_idf`
- `apps/linux_sim`
- `apps/linux_rpi`

Responsibilities:

- process / firmware entrypoints
- target selection
- composition root wiring
- startup sequencing
- build configuration

Forbidden ownership:

- duplicated business logic
- duplicated shared screen logic
- platform adapter internals

### Layer F: External Device Baselines

Primary ownership:

- `M5Stack_Linux_Libs`

Responsibilities:

- vendor-provided Linux device SDK concerns

Rule:

- This layer is beneath Trail Mate's platform boundary.
- It may be consumed by `platform/linux/rpi` and the Pi shell, but it must not
  become a shortcut around our contracts.

## 4. Legal dependency direction

The dependency direction must be:

```text
apps/* -> modules/ui_shared -> modules/core_* -> sys utilities
       -> platform/* ------> contracts in modules/core_sys/include/platform/ui/*

platform/* -> contracts
platform/* -> OS / SDK / drivers

modules/ui_shared -> contracts
modules/ui_shared -> core modules

modules/core_* -> no platform implementation
```

More explicitly:

- `modules/core_*` must not depend on `modules/ui_shared`, `apps/*`, or
  `platform/*`.
- `modules/ui_shared` may depend on core modules and platform contracts, but
  not on `platform/esp/*`, `platform/linux/*`, or app-shell code.
- `platform/*` may depend on contracts and platform dependencies, but not on
  app-shell private code.
- `apps/linux_sim` and `apps/linux_rpi` must share the same presentation layer
  through contracts, not by duplicating or forking UI logic.

## 5. Known current violations

These are not abstract concerns; the repository already contains concrete
violations that this specification treats as real debt.

### `ui_shared` still knows ESP Arduino build details

`modules/ui_shared/library.json` currently includes
`../../platform/esp/arduino_common/include` and depends on `Preferences`.

That makes `ui_shared` a platform-dependent package, which violates Layer B.

### `core_chat` still contains platform-specific BLE tails

Examples under `modules/core_chat/src/ble/*` still include `Arduino.h` and
FreeRTOS-related headers.

That means those files are not valid Layer A code even if they live inside a
core module tree today.

### `BoardBase` is not the Linux seam

`platform/shared/include/board/BoardBase.h` remains useful for MCU board
families.
It is not the primary abstraction for the Linux line.

Cardputer Zero must integrate through platform contracts, not by pretending to
be another MCU board subclass.

## 6. Cardputer Zero consequences

These consequences follow directly from the specification.

### `apps/linux_sim`

Owns only:

- desktop process lifecycle
- simulator chrome and geometry
- desktop mouse/keyboard mapping
- simulator-only validation helpers

It must not become a second shared UI tree.

### `apps/linux_rpi`

Owns only:

- Pi OS device entrypoint
- real device composition root
- shell wiring for the device target

It must not absorb simulator concerns or copied demo-app structure.

### `platform/linux/common`

Owns:

- Linux implementations of shared platform contracts
- Linux-safe shell/session helpers that both Linux shells use

It must not absorb desktop-only geometry or Pi-specific device assumptions.

### `platform/linux/rpi`

Owns:

- Pi OS specific integration
- Cardputer Zero device-facing adapters
- `M5Stack_Linux_Libs` consumption points

It should host SDK binding where the Linux common layer would otherwise become
device-specific.

## 7. Structural program before feature work

Feature work should follow this structural program, not replace it.

1. Complete and normalize the contract inventory in
   `modules/core_sys/include/platform/ui/*`.
2. Remove ESP-only build/runtime dependencies from `modules/ui_shared`.
3. Split platform-specific tails out of core-module locations that currently
   violate Layer A, starting with `core_chat` BLE-facing code.
4. Keep Linux shared shell code in `platform/linux/common` and keep both Linux
   app shells thin.
5. Only after those boundaries are trustworthy, choose a verification slice
   such as `Settings`.

The first real page is therefore a proof target, not a source of structure.

## 8. Acceptance checks

This specification is only useful if it can reject bad structure.

The Cardputer Zero adaptation is aligned with this specification only when the
following statements are becoming true:

- A `core_*` module can compile without Arduino, LVGL, board headers, or the
  vendor SDK.
- `ui_shared` can compile without ESP-specific include roots or Arduino-only
  storage primitives.
- `linux_sim` and `linux_rpi` run the same shared shell/presentation logic
  without forking it.
- Replacing a Linux platform implementation does not require changes to the
  shared core or shared presentation layer.
- A new page is validated through the contracts instead of inventing new
  backdoor dependencies.
