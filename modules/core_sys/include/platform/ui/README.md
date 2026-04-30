# `platform::ui::*` Contract Inventory

This directory is the authoritative contract surface between shared
presentation/core code and platform implementations.

Shared layers may depend on these headers.
They must not depend on concrete implementations under `platform/esp/*`,
`platform/linux/*`, simulator glue, board headers, or vendor SDKs.

## Contract rules

- Each header in this directory defines a capability contract, not a platform
  implementation.
- Platform implementations belong under `platform/*`.
- `ui_shared` and other shared layers may include these headers, but must not
  tunnel around them into platform-specific trees.
- Unsupported features should report unsupported through the contract rather
  than forcing shared code to guess from platform details.
- The boundary is guarded by `scripts/check_platform_ui_boundaries.py`, which
  runs in CI and the Linux WSL validation entrypoint.

## Current contract set

- `device_runtime.h`
  Device identity, delay/restart, battery/memory stats, haptics, storage
  presence, and basic capability/state helpers.
- `firmware_update_runtime.h`
  Firmware update support reporting and trigger entrypoints.
- `gps_runtime.h`
  GPS support/state, snapshots, and GNSS-facing UI data access.
- `hostlink_runtime.h`
  Host-link session lifecycle and status.
- `lora_runtime.h`
  LoRa receive-mode acquisition/configuration and instant RSSI access for UI.
- `orientation_runtime.h`
  Heading/orientation support and samples for UI consumers.
- `pack_repository_runtime.h`
  Pack catalog/install contract used by the shared Extensions UI while hiding
  platform storage/network details.
- `route_storage.h`
  Route file existence, load/save, and listing abstractions.
- `screen_runtime.h`
  Screen timeout state, brightness-adjacent sleep policy, and user-activity
  lifecycle hooks.
- `settings_store.h`
  Persistent key/value and blob storage for shared UI/runtime settings.
- `sstv_runtime.h`
  SSTV support and session lifecycle hooks.
- `team_ui_store_runtime.h`
  Team UI snapshot/chatlog/track persistence seam consumed by shared Team,
  Chat, Contacts, and dashboard presentation.
- `team_ui_types.h`
  Shared Team UI value types and color helpers that must stay usable from both
  the presentation shell and platform persistence seams.
- `time_runtime.h`
  Timezone persistence and local-time helpers.
- `tracker_runtime.h`
  Tracker support/state and route/recording lifecycle.
- `usb_support_runtime.h`
  USB support reporting and page-facing lifecycle hooks.
- `walkie_runtime.h`
  Walkie-talkie support/state lifecycle.
- `wifi_runtime.h`
  Wi-Fi config persistence, scan/connect lifecycle, and status.

## Missing-contract smell

If shared code needs any of the following directly, the right response is
usually "a contract is missing" rather than "add another platform include":

- `Arduino.h`
- `Preferences`
- `SD.h`, `FS.h`
- Linux device paths
- `esp_*` APIs
- board-specific types

That smell should trigger a contract decision before more implementation work.
