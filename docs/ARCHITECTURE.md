# Architecture and Repository Direction

## Why this document exists

Trail Mate is no longer a single-platform firmware project.
It already has:

- an Arduino + PlatformIO environment for existing ESP32 handheld devices
- an ESP-IDF-based Tab5 adaptation experiment
- a planned Linux line for Raspberry Pi class devices and UNO Q class devices

The project therefore needs a **clear long-term repository structure**.
This document defines:

- what problem the repository structure is solving
- what the repository should look like in the future
- what kinds of code belong to shared modules vs platform adapters
- how the project should evolve to support `PIO + IDF + Linux` without maintaining multiple full copies of the same source tree

This is a direction document first, and an implementation plan second.

---

## Current problem

At the moment, the project has already started moving toward reusable domain logic, but the repository still has a major structural risk:

- shared code and platform code are still mixed in the same `src/` tree
- some experiments have already grown into a second full project tree (`trail-mate-idf`)
- UI, platform entrypoints, board support, BLE, storage, timers, and system APIs are still tightly mixed in several places
- the same business logic is at risk of being copied and maintained in parallel across multiple repositories

The main waste is not lack of abstraction.
The main waste is **duplicate maintenance of nearly identical source trees**.

If this continues, every new platform will multiply maintenance cost:

- PlatformIO firmware
- ESP-IDF firmware
- Linux simulator
- Raspberry Pi device target
- UNO Q device target

That is not sustainable.

---

## Architecture goals

The repository structure should achieve the following goals.

### 1. One main repository

There should be a single main repository for Trail Mate.
Platform-specific targets should become application shells inside the main repository, not separate long-term full-code forks.

### 2. One shared core, many shells

The project should converge toward:

- one shared business/core layer
- multiple platform adapters
- multiple application entry shells

### 3. No duplicate source trees

The same chat logic, GPS logic, team logic, and hostlink logic should not exist in separate copies for:

- PlatformIO
- ESP-IDF
- Linux

### 4. Clear platform boundaries

Core modules must not directly depend on:

- Arduino
- ESP-IDF
- FreeRTOS
- LVGL
- board-specific GPIO definitions
- hardware drivers

Those belong in platform adapters.

Shared include prefixes should reflect that boundary:

- `chat/...`, `gps/...`, `team/...` are reserved for cross-platform module APIs
- platform-specific concrete implementations should be exposed under platform-prefixed paths such as `platform/esp/arduino_common/...`

### 5. Linux becomes a first-class target

The future Linux line is not a side experiment.
The structure must allow:

- `linux_sim` first
- then `linux_rpi`
- then `linux_unoq`

without forcing another repository split.

### 6. Keep migration incremental

This structure must be reachable through incremental refactoring.
It must not require a one-shot rewrite of the entire project.

---

## Target repository structure

The long-term target structure should look like this:

```text
trail-mate/
  apps/
    esp_pio/
    esp_idf/
      targets/
        tab5/
        t_display_p4/
    linux_sim/
    linux_rpi/
    linux_unoq/

  modules/
    core_sys/
    core_chat/
    core_gps/
    core_team/
    core_hostlink/
    ui_shared/

  platform/
    esp/
      arduino_common/
      idf_common/
      boards/
        tdeck/
        twatchs3/
        tlora_pager/
        tab5/
        t_display_p4/
    linux/
      common/
      rpi/
      unoq/

  docs/
  tools/
  third_party/
```

This structure has three layers with different responsibilities.

---

## Layer responsibilities

## `modules/`: shared business and reusable logic

`modules/*` should contain code that is reusable across all environments.

Examples:

- chat domain models
- chat use cases
- contact logic
- mesh protocol state machines that do not depend on actual BLE/GATT APIs
- GPS filtering and route logic
- team/domain/protocol logic
- hostlink protocol/state logic
- persistence policies and serialization rules
- pure utility code in `sys`

This layer should be the most stable and the most reusable.

### Important rule

`modules/*` must not directly include or depend on platform headers such as:

- `Arduino.h`
- `Preferences.h`
- `HardwareSerial`
- `esp_*`
- `freertos/*`
- `lvgl.h`
- board headers

If code needs any of those, it probably does **not** belong in `modules/*`.

---

## `platform/`: hardware, OS, runtime, and framework adapters

`platform/*` contains implementations of platform-dependent interfaces.

Examples:

- BLE transport implementations
- LoRa radio transport implementations
- GPS hardware drivers
- file system adapters
- persistent storage adapters
- timers and clock providers
- board support and GPIO mapping
- display drivers
- input drivers
- USB support
- audio codec platform glue

This layer is where platform differences belong.

Examples of likely mappings from the current tree:

- `src/board/*` -> `platform/esp/boards/*`
- `src/ble/*` -> `platform/esp/...` or later `platform/linux/...`
- `src/display/*` -> `platform/...`
- `src/input/*` -> `platform/...`
- `src/audio/codec/*` -> `platform/esp/arduino_audio_codec/audio/codec/*`

---

## `apps/`: composition and entrypoints

`apps/*` should be thin application shells.

They should be responsible for:

- platform-specific startup
- selecting which boards/features are enabled
- wiring together modules and platform adapters
- providing the actual `main.cpp` / startup code
- selecting build configuration

They should **not** become another place where business logic grows.

The goal is that each app shell is mainly composition, not implementation.

For ESP-IDF specifically, board choice should be explicit at the app-shell level via `TRAIL_MATE_IDF_TARGET`, not inferred from chip family macros such as `CONFIG_IDF_TARGET_*`, because multiple boards can share the same SoC.

Examples:

- `apps/esp_pio/` builds existing Arduino/PlatformIO targets
- `apps/esp_idf/` becomes the common IDF-based shell root
- `apps/esp_idf/targets/tab5/` and `apps/esp_idf/targets/t_display_p4/` become the first two real IDF targets
- `apps/esp_idf/` is the shared IDF shell root, and `apps/esp_idf/targets/*` carries per-target metadata and selection
- `apps/linux_sim/` becomes the first Linux validation target

---

## Recommended module split

The first batch of shared modules should focus on the highest reuse value.

### First batch

- `core_sys`
- `core_chat`
- `core_gps`
- `core_team`
- `core_hostlink`

### Second batch

- protocol codecs
- persistence policies
- map tile and route-related pure logic
- reusable non-visual UI state/presenter logic

### UI guidance

Do **not** attempt to fully abstract all UI at the beginning.
That is high risk and low immediate return.

Instead:

- move page state and presenter-like logic gradually into `ui_shared/`
- keep layout trees, LVGL object creation, and device-specific interaction details close to the app/platform layer for now
- promote only truly shared UI components, such as reusable navigation helpers, shared screen contracts, generic app-runtime helpers, and pure UI formatters

---

## Dependency rules

The most important architectural rule is dependency direction.

### Core modules may depend on:

- standard C/C++
- internal domain/usecase/port abstractions
- data model interfaces
- pure utility code

### Core modules may not depend on:

- Arduino APIs
- ESP-IDF APIs
- FreeRTOS APIs
- LVGL
- board classes
- device drivers
- GPIO constants

### Platform adapters may depend on:

- framework APIs
- RTOS APIs
- hardware drivers
- shared module interfaces

### Apps may depend on:

- shared modules
- platform adapters
- build/runtime configuration

This keeps the structure stable as the number of supported environments grows.

---

## Port and adapter model

The project already contains good early examples of the intended direction.

Examples already visible in the codebase include patterns such as:

- usecase
- ports/interfaces
- infra/adapter implementations

This should become the default pattern for shared logic:

```text
module/
  domain/
  ports/
  usecase/
  infra/    # only if still platform-neutral
```

If an `infra` implementation depends on Arduino, ESP-IDF, FreeRTOS, LVGL, or board-specific APIs, it should move out of the module and into `platform/*`.

---

## Build system direction

## CMake should define shared module structure

CMake is the most natural common build description because:

- ESP-IDF already uses CMake
- Linux targets naturally use CMake
- explicit targets make module boundaries clearer than large recursive source globs

Each shared module should eventually become an explicit library target.

### Why this matters

The current pattern of globally collecting large parts of `src/` is convenient in the short term but makes long-term reuse harder.
If Linux becomes a real target, module boundaries need to be visible in the build system, not just in folder names.

## PlatformIO should become a build shell, not a separate source universe

PlatformIO should continue to exist, but only as:

- a convenient build and flashing entrypoint for ESP targets
- a consumer of shared `modules/*` plus `platform/esp/*`

It should not justify keeping a second copy of core logic.

---

## Environment strategy

## `apps/esp_pio`

Purpose:

- continue supporting existing handheld ESP targets under PlatformIO
- reuse shared modules and platform adapters

## `apps/esp_idf`

Purpose:

- host the shared ESP-IDF app shell for multiple ESP-IDF boards
- validate the common architecture against a non-Arduino ESP runtime
- keep per-board target descriptors under `apps/esp_idf/targets/*` instead of growing one full shell per board

This should replace the need for separate long-term IDF-side full projects.

## `apps/linux_sim`

Purpose:

- validate the shared core outside ESP
- enable faster iteration without board flashing
- become the first proof that the architecture is genuinely portable

This target should come before `linux_rpi` and `linux_unoq`.

## `apps/linux_rpi` and `apps/linux_unoq`

Purpose:

- provide actual Linux device targets after the simulator proves the shared core is portable

The platform differences here should stay in `platform/linux/*`, not leak back into shared modules.

---

## Migration principles

This repository should **not** be restructured through a one-shot rewrite.
The migration should be incremental.

### Principle 1: stop growing duplicate trees

Once the repository direction is accepted, avoid continuing to evolve multiple full source trees independently.

### Principle 2: move boundaries before moving everything

It is more important to establish the correct dependency boundaries than to instantly move every file into its final directory.

### Principle 3: migrate by vertical slice

Migrate one logical area at a time, such as:

- `core_sys`
- then `core_chat`
- then `core_gps`
- then `core_team`
- then `core_hostlink`

Each slice should still build somewhere before the next one starts.

### Principle 4: prefer thin wrappers during transition

Temporary wrappers, forwarding headers, and compatibility glue are acceptable if they reduce migration risk.

### Principle 5: Linux simulation is a milestone

A module is not truly portable until it has been exercised in a non-ESP environment.

---

## Recommended migration order

A practical order for this repository is:

### Phase 1: establish skeleton and rules

- create `apps/`, `modules/`, and `platform/` directory skeletons
- define module boundaries in documentation
- stop adding new cross-platform logic directly into mixed platform code where possible

### Phase 2: extract `core_sys`

- isolate generic time/utility/config-independent helpers
- introduce common interfaces like `IClock`, `ITimer`, `IStorage`, `IFileSystem` where needed

### Phase 3: extract `core_chat`

- move chat domain/usecase/port logic into a shared module
- keep storage/network/BLE/LoRa side effects in platform adapters

### Phase 4: extract `core_gps`

- move GPS usecase/filtering/route logic into shared core
- keep hardware access and serial/device specifics in platform adapters

### Phase 5: extract `core_team` and `core_hostlink`

- move protocol logic and state machines into modules
- keep radio and transport specifics in platform adapters

### Phase 6: create `linux_sim`

- build shared modules outside ESP
- use mock or simulated platform adapters
- prove the architecture works before targeting real Linux hardware

### Phase 7: converge on `apps/esp_idf` + IDF targets

- reduce the separate IDF experiments into one shared app shell plus explicit per-board targets

### Phase 8: add real Linux device targets

- `linux_rpi`
- `linux_unoq`

---

## What success looks like

The repository structure is successful when all of the following are true:

- adding a new platform does not require duplicating the whole source tree
- chat/GPS/team/hostlink logic is implemented once
- platform-specific details stay in adapters and app shells
- PlatformIO and ESP-IDF both build from the same shared core modules
- Linux simulation can exercise core logic without ESP headers or Arduino runtime
- new contributors can tell where code should go without guessing

---

## Practical contributor rules

When adding new code, use these rules.

### Put code in `modules/*` if it is:

- business logic
- protocol logic without direct platform APIs
- reusable data transformation or state management
- portable across ESP and Linux

### Put code in `platform/*` if it touches:

- Arduino
- ESP-IDF
- FreeRTOS
- LVGL
- GPIO
- BLE stack APIs
- serial ports
- SD/flash implementation details
- audio codec drivers
- display/input drivers

### Put code in `apps/*` if it is about:

- startup
- board selection
- feature composition
- dependency wiring
- build target selection

### Avoid in shared code:

- `#ifdef ARDUINO...`
- `#ifdef ESP_PLATFORM`
- `millis()`
- `Preferences`
- `HardwareSerial`
- `esp_*`
- `lvgl`

If you need those directly, the code probably belongs in a platform adapter.

---

## Final note

This document describes the architectural direction of Trail Mate.
It does not require every file to move immediately.

The key idea is simple:

**one main repository, one shared core, many platform shells**

That is the only sustainable way for Trail Mate to support:

- existing PlatformIO handheld firmware
- ESP-IDF-based targets
- Linux simulator targets
- future Raspberry Pi and UNO Q devices

without turning repository maintenance into duplicated work.

