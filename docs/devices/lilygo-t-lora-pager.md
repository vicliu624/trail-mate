# LilyGo T-LoRa Pager

This document records the board-level hardware facts currently used by this
repository for `LilyGo T-LoRa Pager`.

It exists to prevent drift between:

- the real LilyGo T-LoRa Pager hardware
- this repository's PlatformIO board / variant / environment definitions
- the ESP board runtime implementation that actually brings the hardware up

## Summary

- MCU: `ESP32-S3`
- Flash / PSRAM: `16MB QSPI flash + 8MB QSPI PSRAM`
- Display: `ST7796` SPI TFT
- UI resolution used by this repo: `480x222`
- Radio: `SX1262` or `SX1280` depending on build environment
- GNSS: `u-blox MIA-M10Q`
- Input: `rotary encoder + center key + I2C keyboard`
- Power / battery: `BQ25896 + BQ27220`
- RTC: `PCF85063`
- NFC: `ST25R3916`
- Motion sensor: `BHI260AP`
- Audio codec: `ES8311`
- GPIO expander: `XL9555`

## Physical Layout

The original vendor overview included a useful front-panel sketch. It is kept
here because it helps quickly identify the visible controls and connectors when
working with real hardware:

```text
/---------------------------------------------------\
| ┌───────────────────────────────────────────┐ |-| |
| |                                           | |/| |
| |               480 x 222 IPS               | |/| |
| |                                           | |/| |
| └───────────────────────────────────────────┘ |-| |
|                                                   |
|                                                   |
|                                                   |
|                                                   |
\---|RST|--|BOOT|--|POWER|--|SD SOCKET|--|USB-C|----/
      ^       ^       ^          ^           ^
      |       |       |          |           |
      |       |       |          |           └─── The adapter is used as a charging and
      |       |       |          |                programming interface, and the USB-C can
      |       |       |          |                be programmed to power external devices
      |       |       |          |
      |       |       |          └────── Supports up to 32 GB SD memory card
      |       |       |
      |       |       └───────────── The power button is only valid when the device is
      |       |                     turned off and cannot be customized or program controlled.
      |       |
      |       └───────────────────── (GPIO0) Custom Button or Enter download Mode
      |
      └───────────────────────────── Click to reset the device,
                                     it cannot be programmed or controlled by the program
```

## Board Ownership

Primary board definition files:

- [boards/lilygo-t-lora-pager.json](../../boards/lilygo-t-lora-pager.json)
- [pins_arduino.h](../../variants/lilygo_tlora_pager/pins_arduino.h)
- [tlora_pager.ini](../../variants/lilygo_tlora_pager/envs/tlora_pager.ini)
- [tlora_pager_board.cpp](../../boards/tlora_pager/src/tlora_pager_board.cpp)
- [tlora_pager_board.h](../../boards/tlora_pager/include/boards/tlora_pager/tlora_pager_board.h)

Rules:

- pin truth belongs in `variants/lilygo_tlora_pager/pins_arduino.h`
- board bring-up behavior belongs in `boards/tlora_pager/src/tlora_pager_board.cpp`
- environment-specific radio and display choices belong in `variants/lilygo_tlora_pager/envs/tlora_pager.ini`
- device docs should reflect what this repository actually builds, not just vendor marketing material

## Important Boundary

This repository uses the LilyGo Pager as an ESP board with its own runtime
implementation in [tlora_pager_board.cpp](../../boards/tlora_pager/src/tlora_pager_board.cpp).

That means the most authoritative sources for day-to-day maintenance are:

- `pins_arduino.h` for GPIO ownership
- `tlora_pager.ini` for enabled features per environment
- `tlora_pager_board.cpp` for initialization order and power sequencing

If external vendor docs disagree with runtime behavior here, prefer the checked-in
board runtime unless real hardware verification proves otherwise.

## Build Environments

Defined in [tlora_pager.ini](../../variants/lilygo_tlora_pager/envs/tlora_pager.ini):

- `tlora_pager_sx1262`
- `tlora_pager_sx1262_debug`
- `tlora_pager_sx1280`
- `tlora_pager_sx1280_debug`

Current build-time facts:

- all Pager environments define `ARDUINO_T_LORA_PAGER`
- `SX1262` builds define `ARDUINO_LILYGO_LORA_SX1262`
- `SX1280` builds define `ARDUINO_LILYGO_LORA_SX1280`
- the display driver is built as `ST7796`
- this repo currently builds the Pager UI with `SCREEN_WIDTH=480` and `SCREEN_HEIGHT=222`

## Verified Pin Map

The pin map below is taken from
[pins_arduino.h](../../variants/lilygo_tlora_pager/pins_arduino.h),
which is the active variant source for this repo.

### Shared I2C Bus

- SDA: `3`
- SCL: `2`

Devices sharing this bus include:

- `BHI260AP`
- `PCF85063`
- `BQ25896`
- `BQ27220`
- `DRV2605`
- `ES8311`
- `XL9555`
- `TCA8418`

### Shared SPI Bus

- MOSI: `34`
- MISO: `33`
- SCK: `35`

Bus users:

- LoRa radio
- SD card
- NFC
- display

### External UART / Expansion Header

- TX: `43`
- RX: `44`
- custom external pin: `9`

### Buttons And Input

- Power / wake button: `0`
- Boot / custom button: `9`
- Rotary A: `40`
- Rotary B: `41`
- Rotary center: `7`
- Keyboard interrupt: `6`
- Keyboard backlight: `46`

Notes:

- this board uses a rotary encoder instead of a 5-way joystick
- the keyboard is handled through `TCA8418`
- the boot / power buttons are not interchangeable in behavior

### GNSS

- TX: `12`
- RX: `4`
- PPS: `13`

### LoRa

- CS: `36`
- RESET: `47`
- BUSY: `48`
- IRQ / DIO: `14`
- SPI bus: shared on `34/33/35`

### Display

- Driver: `ST7796`
- CS: `38`
- DC: `37`
- RESET: `-1` (`not connected`)
- Backlight: `42`
- SPI bus: shared on `34/33/35`

### SD Card

- CS: `21`
- SPI bus: shared on `34/33/35`

### Audio

- I2S WS: `18`
- I2S SCK: `11`
- I2S MCLK: `10`
- I2S data out: `45`
- I2S data in: `17`

### Interrupt Pins

- RTC interrupt: `1`
- NFC interrupt: `5`
- Motion sensor interrupt: `8`

### NFC

- ST25R3916 CS: `39`
- ST25R3916 interrupt: `5`

## XL9555 Power / Control Lines

The Pager uses an `XL9555` I/O expander to gate multiple peripherals.

Current logical assignments from
[pins_arduino.h](../../variants/lilygo_tlora_pager/pins_arduino.h):

- `EXPANDS_DRV_EN = 0`
- `EXPANDS_AMP_EN = 1`
- `EXPANDS_KB_RST = 2`
- `EXPANDS_LORA_EN = 3`
- `EXPANDS_GPS_EN = 4`
- `EXPANDS_NFC_EN = 5`
- `EXPANDS_GPS_RST = 7`
- `EXPANDS_KB_EN = 8`
- `EXPANDS_GPIO_EN = 9`
- `EXPANDS_SD_DET = 10`
- `EXPANDS_SD_PULLEN = 11`
- `EXPANDS_SD_EN = 12`

Operationally this means power sequencing for LoRa, GNSS, NFC, keyboard, SD, audio
and haptics is not just raw GPIO configuration on the ESP32-S3. The expander state
also matters.

## Feature Flags

The active variant declares these board capabilities:

- `USING_AUDIO_CODEC`
- `USING_XL9555_EXPANDS`
- `USING_PPM_MANAGE`
- `USING_BQ_GAUGE`
- `USING_INPUT_DEV_ROTARY`
- `USING_INPUT_DEV_KEYBOARD`
- `USING_ST25R3916`
- `USING_BHI260_SENSOR`
- `HAS_SD_CARD_SOCKET`

These flags are part of the board contract and are relied on by the ESP platform code.

## Runtime Bring-Up Notes

The board runtime in
[tlora_pager_board.cpp](../../boards/tlora_pager/src/tlora_pager_board.cpp)
currently initializes or manages:

- battery gauge `BQ27220`
- PMU `BQ25896`
- GPIO expander `XL9555`
- motion sensor `BHI260AP`
- RTC `PCF85063`
- NFC `ST25R3916`
- keyboard `TCA8418`
- audio codec `ES8311`
- LoRa radio
- display and related power lines

When debugging missing peripherals on Pager, always check both:

1. the raw pin assignment
2. the relevant `XL9555` enable line or runtime init path

## Sleep / Power Ownership Notes

- GPS power is owned by `HalGps` / `GpsService`, not by screen sleep helpers.
- NFC power is board-owned and is toggled by Pager board runtime entrypoints such as
  `initNFC()`, `startNFCDiscovery()`, `stopNFCDiscovery()`, and screen sleep.
- The motion sensor is initialized by the board runtime, but Pager does not currently
  implement a dedicated `POWER_SENSOR` hardware branch in `powerControl()`. Do not
  assume screen sleep physically power-cycles `BHI260AP`.
- Walkie temporarily switches the radio into FSK mode. Pager now caches the last
  normal LoRa config so the board can restore the previous LoRa parameters when
  Walkie exits before the higher-level mesh config path re-applies its own runtime state.

## Display Notes

There are two dimensions worth remembering:

- vendor-facing panel spec is often described as `480 x 222`
- this repository also builds with `SCREEN_WIDTH=480` and `SCREEN_HEIGHT=222`

The panel is driven through `ST7796`, and physical orientation / UI rotation should be
validated in runtime code rather than assumed from the raw panel numbers alone.

## Known Risks / Maintenance Notes

- Pager hardware is highly multiplexed. A peripheral can fail because of shared-bus
  contention, expander power state, or init order, not just because a GPIO number is wrong.
- LoRa, SD, NFC and display all share the SPI bus, so bus ownership issues are realistic.
- Many auxiliary devices share the same I2C bus, so probe order and bus locking matter.
- Some apparent power-control paths are logical rather than physical. Maintenance changes
  should avoid assuming every peripheral named in sleep or init code has a real board-level
  power gate unless `powerControl()` actually implements it.

## Maintenance Guidance

When changing this board next time:

1. Update [pins_arduino.h](../../variants/lilygo_tlora_pager/pins_arduino.h) first for GPIO truth.
2. Update [tlora_pager.ini](../../variants/lilygo_tlora_pager/envs/tlora_pager.ini) if the radio or build flags change.
3. Update [tlora_pager_board.cpp](../../boards/tlora_pager/src/tlora_pager_board.cpp) for init order, power gating or runtime behavior.
4. Keep this document aligned with the checked-in implementation, not with stale vendor copy.
