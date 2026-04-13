# GAT562 Mesh EVB Pro

This document records the board-level hardware facts currently verified for
`gat562_mesh_evb_pro`.

It exists to prevent future drift between:

- the real GAT562 Mesh EVB Pro hardware
- this repository's board/variant definitions
- `.tmp/meshtastic-firmware` reference variants used during the original bring-up

## Summary

- MCU: `nRF52840`
- Framework/BSP: `Adafruit nRF52 Arduino`
- Radio: `SX1262`
- Display: `128x64 SSD1306 OLED` over `I2C`
- GNSS: `UART + PPS`
- Input: `5-way joystick + 2 buttons`
- BLE: enabled
- Meshtastic / MeshCore: enabled

## Important Boundary

`GAT562 Mesh EVB Pro` is not the same thing as the Meshtastic
`gat562_mesh_trial_tracker` reference variant.

Some early board definitions were aligned from `.tmp/meshtastic-firmware`, but that
reference carries RAK/WisBlock-style inherited definitions that do not all match the
real EVB Pro hardware.

The most important verified difference at the time of writing:

- `GAT562 Mesh EVB Pro` should be treated as not having on-board external QSPI flash
- the old inherited `PIN_QSPI_*` / `EXTERNAL_FLASH_*` definitions were removed from
  [variant.h](../../variants/gat562_mesh_evb_pro/variant.h)

When adapting this board further, prefer verified EVB Pro hardware facts over
reference-variant inheritance.

## Board Ownership

Primary board definition files:

- [boards/gat562_mesh_evb_pro.json](../../boards/gat562_mesh_evb_pro.json)
- [board_profile.h](../../boards/gat562_mesh_evb_pro/include/boards/gat562_mesh_evb_pro/board_profile.h)
- [gat562_board.h](../../boards/gat562_mesh_evb_pro/include/boards/gat562_mesh_evb_pro/gat562_board.h)
- [gat562_board.cpp](../../boards/gat562_mesh_evb_pro/src/gat562_board.cpp)
- [variant.h](../../variants/gat562_mesh_evb_pro/variant.h)
- [gat562_mesh_evb_pro.ini](../../variants/gat562_mesh_evb_pro/envs/gat562_mesh_evb_pro.ini)

Rules:

- board-level pin truth belongs in `boards/` and `variants/`
- app/runtime code must not hardcode raw pins
- if Meshtastic reference variants disagree with verified EVB Pro behavior, EVB Pro wins

## Verified Pin Map

The currently verified EVB Pro pin map in this repository is:

### LEDs

- Status LED: `35`
- Notification LED: `36`

### Buttons And Joystick

Defined in [board_profile.h](../../boards/gat562_mesh_evb_pro/include/boards/gat562_mesh_evb_pro/board_profile.h).

- Primary button: `9`
- Secondary button: `12`
- Joystick up: `28`
- Joystick down: `4`
- Joystick left: `30`
- Joystick right: `31`
- Joystick press: `26`

Notes:

- joystick inputs are currently treated as `active low`
- pull-ups are enabled for both buttons and joystick
- joystick center was previously misconfigured as `10`
- real-device verification showed that `26` is the correct joystick center pin for EVB Pro

### OLED

- SDA: `13`
- SCL: `14`
- Address: `0x3C`

### GNSS

- RX: `15`
- TX: `16`
- PPS: `17`
- Baud rate: `9600`

### LoRa / SX1262

- CS: `42`
- DIO1: `47`
- BUSY: `46`
- RESET: `38`
- POWER_EN: `37`
- SPI SCK: `43`
- SPI MISO: `45`
- SPI MOSI: `44`

### Other Board Control

- Peripheral 3V3 enable: `34`
- Battery ADC: `5`

## NFC Pins

This board still uses nRF52840 NFC-capable pins in its GPIO space.

- `PIN_NFC1 = 9`
- `PIN_NFC2 = 10`

For that reason the environment enables:

- `CONFIG_NFCT_PINS_AS_GPIOS=1`

See:

- [gat562_mesh_evb_pro.ini](../../variants/gat562_mesh_evb_pro/envs/gat562_mesh_evb_pro.ini)

This is required so `P0.09` / `P0.10` behave as normal GPIOs instead of NFC pins.

## Input Debugging Notes

Useful debug path during bring-up:

- board raw input logs are emitted from [gat562_board.cpp](../../boards/gat562_mesh_evb_pro/src/gat562_board.cpp)
- UI input logs are emitted from [ui_runtime.cpp](../../apps/gat562_mesh_evb_pro/src/ui_runtime.cpp)

Typical healthy joystick logs look like:

```text
[gat562][board] raw in pri=0 sec=0 up=0 down=0 left=0 right=0 press=1 any=1
[gat562][ui] input key=JoyPress pressed=1 action=Select
```

If direction keys work but center press does not log at board level, the first thing to
suspect is pin mapping, not UI logic.

## Known History

These facts were learned during bring-up and should not be reintroduced accidentally:

- `joystick_press = 10` was wrong for EVB Pro
- broad GPIO probing caused instability and should not be used casually on this board
- inherited external QSPI flash definitions from reference variants were misleading for EVB Pro

## Maintenance Guidance

When changing this board next time:

1. Update `board_profile.h` first if the change is a runtime hardware mapping fact.
2. Update `variant.h` only for real BSP-visible hardware definitions.
3. Prefer narrow, board-safe validation logs over broad GPIO probing.
4. Keep this document in sync with verified hardware behavior.
