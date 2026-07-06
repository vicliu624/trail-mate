# T-Impulse Plus Target Spec

This document records the Trail Mate integration contract for the LILYGO
T-Impulse Plus nRF52840 target. It intentionally separates product behavior,
shared app-shell behavior, and board-level hardware facts so future fixes do
not grow a private protocol side path for this target.

## Product Position

- Target id: `t-impulse-plus`.
- Build environment: `builds/pio_nrf52`, PlatformIO env `t-impulse-plus`.
- Board package: `boards/t_impulse_plus`.
- Variant package: `variants/t_impulse_plus_nrf52840`.
- The device is a local LoRa mesh node. It can receive and flood mesh packets
  without a phone connection.
- A phone app is required for active user workflows such as composing messages,
  changing most configuration, and Meshtastic/MeshCore companion behavior.
- The device must support both Meshtastic and MeshCore through the existing
  nRF52 app shell and `IRadioPacketIo -> IMeshAdapter` receive/transmit path.
  It must not publish packets to BLE or contacts through a board-specific side
  path.

## Hardware Facts

- MCU: nRF52840.
- Radio: SX1262, board-level RadioLib driver owned by `boards/t_impulse_plus`.
- Display: physical 128x64 SSD1306 I2C panel, constrained to a logical
  `screen_64x32` status layout.
- Input: Reset and Boot are board/system buttons. The integration exposes only
  the touch/function input as product interaction.
- GPS: enabled. It is used for clock sync and low-rate position updates.
- IMU: not integrated for this target.
- Battery, vibration, display, LoRa, GPS, and storage details are board package
  responsibilities.

## UI Contract

- T-Impulse Plus does not render chat/message content.
- The firmware uses a tiny one-line status UI instead of `ui_mono::Runtime`.
- Default line alternates by user action between time and current protocol.
- Meshtastic BLE PIN is displayed only while Meshtastic pairing is active,
  passkey is required, and BLE is not connected. The PIN value is owned by the
  BLE module; the board/UI only projects `BlePairingStatus`.
- MeshCore must not invent or display a fake PIN.
- Short press toggles the one-line view between time and protocol.
- Long press on the protocol view enters switch-confirm mode.
- Long press in switch-confirm mode calls the shared app facade protocol switch
  path with persistence and then restarts the device.
- Short press in switch-confirm mode cancels.
- Button press/release/short/long events are logged for bring-up diagnostics.

## Feature Exclusions

- Do not compile `ui_mono` into the `t-impulse-plus` firmware.
- Do not compile CJK fonts or pinyin IME for this target.
- Do not show message bodies, contact lists, keyboards, or full settings pages
  on the device display.
- Do not add T-Impulse Plus behavior to the T-Echo Lite board package.
- Do not use T-Echo Lite board code as a hardware reference. T-Echo Lite may
  only be used as an example of app-shell contracts and interface boundaries.

## Architecture Contract

- `modules/product_composition` owns target metadata:
  - target profile
  - UX binding
  - build binding
- `builds/pio_nrf52/platformio.ini` owns the PlatformIO environment and compile
  flags.
- `apps/nrf52_node` owns the app-shell composition. T-Impulse Plus may select a
  target-specific tiny UI branch there, but shared protocol and BLE behavior
  must remain module-owned.
- `boards/t_impulse_plus` owns:
  - board profile and pins
  - SSD1306 display driver
  - SX1262 `IRadioPacketIo`
  - GPS runtime and `platform::ui::gps` binding
  - touch/function input runtime
  - settings persistence wrapper
- `platform/nrf52/arduino_common` owns shared nRF52 BLE, Meshtastic, MeshCore,
  storage, and radio adapter behavior. T-Impulse Plus fixes should avoid
  changing this layer unless the bug is proven to be shared.

## Build Constraints

- `TRAILMATE_TARGET_T_IMPULSE_PLUS=1` selects the target.
- `TRAILMATE_NRF52_TINY_STATUS_UI=1` documents the tiny UI mode.
- `TRAIL_MATE_NRF_MONO_SCREEN_64X32_ONLY=1`, `SCREEN_WIDTH=64`, and
  `SCREEN_HEIGHT=32` document the constrained logical layout.
- `GAT562_NO_CJK=1` and `GAT562_NO_PINYIN_IME=1` keep message text input and
  Chinese font/input support out of this firmware.
- The app shell manifest may cause PlatformIO to install `ui_mono` as a known
  library, but the `t-impulse-plus` environment must keep `ui_mono` out of the
  dependency graph and link through `lib_ignore = ui_mono`.
- Default upload protocol is `nrfutil`, matching the official demo and the
  Adafruit nRF52 bootloader flow. J-Link remains available as an explicit
  recovery/debug protocol, but it is not the default.

## Verification Baseline

Required before merging changes to this target:

- `pio run --project-dir builds/pio_nrf52 -e t-impulse-plus`
- `pio run --project-dir builds/pio_nrf52 -e t-echo-lite`

The second build guards the shared nRF52 app shell and confirms T-Echo Lite was
not regressed by target selection changes.
