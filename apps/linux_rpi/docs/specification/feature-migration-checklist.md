# Cardputer Zero Feature Migration Checklist

This checklist turns the current Cardputer Zero assessment into a concrete
implementation order.

It is subordinate to
[cardputer-zero-adaptation-spec.md](./cardputer-zero-adaptation-spec.md).
This checklist must validate that specification, not redefine it.

It is intentionally opinionated:

- build the real device shell on top of `M5Stack_Linux_Libs`
- reuse shared business logic from `modules/*` where the boundary is already
  real
- avoid forcing Linux through MCU board abstractions too early
- keep unsupported hardware features explicitly stubbed instead of half-ported

## 1. Reuse Buckets

### Reuse Now

These pieces are already good candidates for direct reuse in the Linux line:

- `modules/core_sys`
  portable clock helpers plus shared `platform::ui::*` runtime contracts
- `modules/core_hostlink`
  protocol codecs and session state
- `modules/core_gps`
  domain models, motion policy, jitter filter, runtime policy/state
- `modules/core_team`
  team domain, protocol, and use-case logic
- `modules/core_chat`
  `domain`, `usecase`, and most of `infra`
- `modules/ui_shared`
  LVGL contracts, shared components, shared assets, and formatting helpers

### Reuse With Thin Linux Adapters

These pieces are still the right seam, but they need a Linux-side runtime or
storage implementation first:

- `modules/core_sys/include/platform/ui/device_runtime.h`
- `modules/core_sys/include/platform/ui/time_runtime.h`
- `modules/core_sys/include/platform/ui/screen_runtime.h`
- `modules/core_sys/include/platform/ui/settings_store.h`
- `modules/core_sys/include/platform/ui/wifi_runtime.h`
- `modules/core_sys/include/platform/ui/tracker_runtime.h`
- `modules/core_sys/include/platform/ui/hostlink_runtime.h`
- `modules/core_sys/include/platform/ui/gps_runtime.h`
- `modules/core_sys/include/platform/ui/lora_runtime.h`

### Do Not Reuse As Linux Baseline

These pieces should not be treated as the Linux foundation:

- `M5CardputerZero-UserDemo`
  use only for board hints such as framebuffer naming and likely input-device
  paths
- `modules/core_chat/src/ble/*`
  phone-side BLE shells still include Arduino and FreeRTOS details
- `modules/ui_shared/library.json`
  current build metadata still points at ESP Arduino include paths and
  `Preferences`
- `platform/shared/include/board/*.h`
  still useful for MCU board families, but not the primary Linux seam

## 2. Structural Compliance Before First Feature Slice

Before choosing the first true page migration, these structural tasks come
first:

1. make the `platform::ui::*` contract set explicit and complete
2. remove ESP Arduino include roots and `Preferences` dependence from
   `modules/ui_shared`
3. relocate or split platform-specific code that still lives under `core_*`
   module trees
4. keep shared Linux shell/session code in `platform/linux/common` instead of
   app-shell private directories
5. only then select the first verification slice, likely `Settings`

## 3. First Linux Runtime Slice

Build these Linux implementations first under `platform/linux/common`:

1. `platform::ui::settings_store`
   file-backed key/value persistence for UI and runtime preferences
2. `platform::ui::time`
   timezone offset persistence plus local-time helpers
3. `platform::ui::screen`
   settings-backed timeout state and no-op/soft Linux sleep bookkeeping
4. `platform::ui::device`
   system identity, delays, memory stats, and capability reporting

This is the minimum slice needed to stop `ui_shared` from depending on ESP-only
runtime code for basic shell behavior.

## 4. First-Batch UI Targets

### Enable First

These areas should be the first UI targets after the runtime slice exists:

- shell bootstrapping
  `startup_shell`, `menu_layout`, `menu_runtime`, `app_runtime`
- localization and resource registry
  locale selection, font selection, and persisted language preferences
- settings core
  time zone, screen timeout, vibration preference, debug toggles,
  notification volume, and any Linux-safe capability flags

### Delay Until Data Adapters Exist

These UI areas should wait until the corresponding Linux storage/transport
adapters exist:

- `chat`
  needs Linux chat store and mesh adapter
- `contacts`
  needs chat/contact stores and node metadata pipeline
- `team`
  needs team event sinks, track source, and chat app-data path
- `node_info`
  should follow once chat/contact metadata is flowing

### Stub or Hide Initially

These should start disabled with explicit `is_supported() == false` behavior:

- `gps`
- `gnss`
- `tracker`
- `energy_sweep`
- `pc_link`
- `usb`
- `sstv`
- `walkie_talkie`
- Wi-Fi backed pack download and extensions install flows

The rule is simple: if the hardware or transport does not exist on Cardputer
Zero yet, do not fake a partial port just to keep a menu item visible.

## 5. Second Slice After Runtime

Once the first runtime slice is in place, build the next layer in this order:

1. Linux chat/contact/node stores
2. Linux local or loopback `IMeshAdapter`
3. Linux hostlink runtime if a practical transport exists
4. Linux tracker/file storage adapters
5. Linux Wi-Fi runtime
6. Linux GPS runtime
7. Linux LoRa runtime

This sequence keeps the shared business code moving before the high-risk
hardware work starts.

## 6. Cardputer Zero Specific Work

These are inherently target-facing and should be implemented specifically for
the Cardputer Zero shell:

- fbdev device discovery and LVGL display bring-up
- evdev keyboard mapping for the built-in keypad
- battery and power-state detection if the Linux userland exposes it
- mount-point detection for removable storage
- audio/haptic behavior only if the device exposes a practical Linux path

`M5Stack_Linux_Libs` should own as much of this generic Linux device plumbing as
possible.

## 7. Current Start Line

The current repository should proceed from here:

1. land the final-shape specification and make migration docs obey it
2. finish structural compliance on `ui_shared`, contracts, and misplaced
   platform tails
3. keep `apps/linux_rpi` on the `M5Stack_Linux_Libs` device shell path
4. begin feature verification only after the structural program is credible
