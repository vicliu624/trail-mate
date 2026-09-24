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

- `map_diagnostics.h`
  Optional map diagnostic output. Shared UI submits formatted records without
  choosing an SDK or output device; platform implementations select Arduino
  serial or the native console. Disabled builds do not evaluate diagnostic calls.

- `capability_status.h`
  Shared `CapabilityState` / `CapabilityStatus` types used by all runtime
  contracts to honestly signal Unsupported / Simulated / Available / Degraded /
  Error.  Every runtime that exposes `capability_status()` includes this header.
- `device_runtime.h`
  Device identity, delay/restart, battery/memory stats, haptics, storage
  presence, and basic capability/state helpers.
- `firmware_update_runtime.h`
  Firmware update support reporting and trigger entrypoints.
- `gps_runtime.h`
  GPS support/state, snapshots, and GNSS-facing UI data access.
- `hostlink_runtime.h`
  Host-link session lifecycle and status.
- `http_client_runtime.h`
  Unified Wi-Fi HTTP/TLS byte-stream access contract. ESP implementations own
  the concrete HTTP client and must be reached through the Wi-Fi access policy
  instead of product features opening HTTP clients directly.
- `lora_runtime.h`
  LoRa receive-mode acquisition/configuration and instant RSSI access for UI.
- `orientation_runtime.h`
  Heading/orientation support and samples for UI consumers.
- `pack_repository_runtime.h`
  Pack catalog/install contract used by the shared Extensions UI while hiding
  platform storage/network details.
- `route_storage.h`
  Route file existence, load/save, and listing abstractions.
- `reticulum_group_config_runtime.h`
  SD-card Reticulum shared group destination configuration contract used by
  Contacts and Reticulum runtime apply paths.
- `reticulum_directory_runtime.h`
  SD-card Reticulum announce and LXMF address directory contract. This is the
  Reticulum network/address-book source used by protocol runtimes; group TSV
  remains a narrower shared-destination configuration file.
- `screen_runtime.h`
  Screen timeout state, brightness-adjacent sleep policy, and user-activity
  lifecycle hooks.
- `screen_brightness_steps.h`
  Single source of truth for the user-visible 10%-100% brightness steps,
  percentage-to-device-level rounding, minimum-level clamping, and shortcut
  cycling. Settings, startup restore, and main-menu shortcuts must use this
  policy instead of defining their own brightness tables or arithmetic.
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
  Walkie-talkie support/state lifecycle, page-independent monitor policy, and
  page-facing PTT control. `Status::active` is the hardware audio/radio session
  truth; `Status::monitor_enabled` is the user policy that keeps listening alive
  after leaving the Walkie page.
- `resource_owner.h`
  Shared `ResourceKind` / `OwnerToken` vocabulary for radio, display,
  power rail, I2C/SPI bus, GPS, and audio ownership across Linux and IDF.
- `wifi_runtime.h`
  Wi-Fi config persistence, scan/connect lifecycle, and status.
- `wifi_access_runtime.h`
  Wi-Fi business access coordination for HTTP, OTA, MQTT, and Reticulum gateway
  users. It owns connection gating, screen/wake policy, exclusivity, and
  traffic budgets above the lower-level Wi-Fi control plane.

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
