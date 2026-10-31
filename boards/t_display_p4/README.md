# `boards/t_display_p4`

Board-owned runtime and hardware truth for `LILYGO T-Display-P4`.

This board family maps to explicit display/radio ESP-IDF targets:

- `t_display_p4_tft`
- `t_display_p4_amoled`
- `t_display_p4_tft_lr2021`
- `t_display_p4_amoled_lr2021`

The original targets select SX1262. The `_lr2021` targets select the native
RadioLib LR2021 packet backend. Display defaults are shared by display type;
the target descriptor selects the radio at compile time. The user-confirmed
TFT + LR2021 device uses `t_display_p4_tft_lr2021`.

Mesh, Protocol Probe, Walkie and quick controls share the selected radio's
singleton, mutex and reference count. The LR2021 backend translates physical
IRQ bits to the existing IDF packet contract, uses XL9535 reset and the board
antenna path, configures 3.0 V TCXO and DIO6/7/8/10 RF switching, and polls IRQ
status with DIO11 configured. BUSY/transport failures make it offline and hold
reset. Raw-IQ streaming and broadcast FM are separate pending capabilities.

On `_lr2021` targets, Protocol Probe includes **IQ Snapshot** (`I` on a keyboard).
It stops probing, captures 256 IQ pairs on the current mesh PHY in a worker,
restores the radio and displays finite diagnostic statistics. It does not
identify a protocol or provide continuous audio. Scan/apply actions wait for
capture/recovery to finish, and exiting joins the worker before another page
can own the radio. An unknown sample rate remains explicitly unqualified.
The TFT firmware compiles and links this path; physical IQ acceptance is pending.

The family-level code here stays shared. Variant selection belongs to the target descriptor layer, not to ad-hoc build commands or undocumented menuconfig toggles.

Primary source reference:

- `.tmp/T-Display-P4`

## Extracted Board Facts

- System I2C: `LP_I2C_NUM_0=2`, `SDA=7`, `SCL=8`
- External I2C: `port=1`, `SDA=20`, `SCL=21`
- GPS UART: `UART1`, host `TX=23`, host `RX=22`
- SDMMC: `D0=39`, `D1=40`, `D2=41`, `D3=42`, `CMD=44`, `CLK=43`
- C6 SDIO link: `CLK=18`, `CMD=19`, `D0=14`, `D1=15`, `D2=16`, `D3=17`
- C6 companion module: present, ESP32-C6, P4-C6 transport target is SDIO
- C6 boot/download strap control: XL9535 `IO13`, assert low / release high
- C6 reset/release control: XL9535 `IO14`, release high / assert low
- IMU: `ICM20948`, I2C address `0x68`
- Audio I2S: `BCLK=12`, `MCLK=13`, `WS=9`, `DOUT=10`, `DIN=11`
- SX1262 SPI: `host=SPI2`, `SCK=2`, `MISO=4`, `MOSI=3`, `CS=24`, `BUSY=6`
- LR2021 uses the same V1 SPI pins; its command protocol and chip RF-switch
  configuration belong to its own backend.
- XL9535 expander:
  - `IO1` = `SKY13453_VCTL`
  - `IO11` = GPS wake
  - `IO13` = C6 boot/download strap control
  - `IO14` = C6 reset/release control
  - `IO15` = SD enable
  - `IO16` = SX1262 reset
  - `IO17` = SX1262 DIO1
- Boot key: `35`
- Expander interrupt: `5`
- Backlight: `51`
- Optional keyboard module:
  - hardware HP I2C0: `SDA=46`, `SCL=45` (SYS uses LP I2C0 on GPIO7/8)
  - TCA8418: `0x34`, 10 x 7 scan window, interrupt `48`
  - keyboard backlight: `47`

## Display Runtime Baseline

The official T-Display-P4 LVGL baseline uses an independent RGB565 draw buffer,
direct DPI-panel flush callbacks, and the ESP32-P4 PPA for rotation. TrailMate
uses the same ownership model: `esp_lvgl_port` allocates an independent
`SPIRAM | DMA` LVGL buffer and a cache-line-aligned PPA output buffer, renders
dirty areas in `PARTIAL` mode, rotates those areas in PPA, then submits them to
the DPI panel. The DPI driver has `num_fbs = 0`; it is not an application-owned
front/back swapchain. LVGL flush completion comes from DSI
`on_color_trans_done`, not a refresh-boundary callback. The complete invariant
is documented in `docs/engineering/t-display-p4-display-runtime-architecture.md`.

## Keyboard Runtime Policy

T-Display-P4 Keyboard is an accessory, not a base-board invariant. Runtime
probing owns the distinction:

- TCA8418 probe succeeds: enable the P4 adapter's physical-key event routing.
- TCA8418 probe fails or the module is absent: keep touch input active and show
  the virtual keyboard on large-touch compose surfaces.

Keyboard backlight is an independent GPIO47/SY7200A PWM output. Board startup
initializes it to approximately 30% brightness even when XL9555/TCA8418 input
initialization fails. The platform backlight capability/get/set APIs use
`supportsKeyboardBacklight()`, not `hasKeyboard()`. These APIs do not themselves
add a touch settings control. Keypad readiness remains independent, and this
change does not alter the touch IME's physical-input availability check.

LDO4 powers VDDPST_5 (GPIO39-48), including keyboard signals and SD signals.
The keyboard connector's ESP_3V3 supply is a separate electrical domain.

The adapter preserves TCA8418 press/release edges in an LVGL keypad device.
It explicitly selects the active menu/app focus group and uses a private proxy
for text input, page-container key handlers, menu shortcuts and application
exit. Native LVGL processing provides Enter activation, Tab focus traversal
and held-key repetition. Page/focus replacement and FIFO overflow cancel any
incomplete key state to prevent clicks or repeats reaching a replacement view.

Probe the controllers before display startup, then attach the keypad under the
LVGL display lock even if the initial probe failed. Optional accessory probes
retry every five seconds on the LVGL task. Three consecutive communication
failures clear readiness and restart that same attach sequence.

## SDMMC Transfer Policy

P4 uses four-bit SDMMC at a requested maximum of 40 MHz. Wio Tracker L2 uses
one-bit SDMMC at 20 MHz; their wiring and host ownership remain board-specific.
The IDF and L2 paths share the SDMMC transfer sizes: 16 KiB file slices, up to
32 sectors per direct-DMA batch, and a 4 KiB bounce buffer for other buffers.
The P4 bounce buffer is static internal RAM with 64-byte alignment, protected
by the SD runtime mutex. Direct transfers use the host's alignment callback.
Routine builds retain rate-limited slow/error logs instead of tracing every
SD operation. The IDF adapter is shared with Tab5, so its batching improvements
also apply there.

## Power Runtime Policy

For P4 V1.0, preload safe XL9535 output levels before switching pins to output
mode and keep the active-low external 3.3 V rail enabled through startup and
software sleep. LilyGO documents peripheral reset anomalies and download-mode
lock when this rail is disabled. Screen/touch reset sequencing is separate
from rail power and must not be implemented by cycling the whole rail.

The accessory's physical power-switch acceptance condition is battery-only
operation with the host switch held ON: the accessory switch must control host
on/off as it does with the official complete UI demo. Software deep sleep is
not equivalent to that hardware behavior; it still requires device validation.

## What Lives Here

This directory should own the hardware facts that stay true even if higher-level runtime code changes:

- board profile data
- board bootstrap and power sequencing
- SYS/EXT I2C ownership
- XL9535 expander access
- RTC access and RTC-to-system-time sync
- GPS UART wake/setup/teardown
- SDMMC power + mount contract
- LoRa-side expander control and verified RF-path assumptions

## What Must Not Drift Here

This directory should **not** become:

- a dual-firmware builder
- the C6 companion firmware implementation
- a fallback bucket for generic shared runtime logic

The board is dual-MCU in hardware. The board package records the C6 hardware
facts, while the in-repository C6 firmware project lives at
`firmware/c6_companion`. The board package must not own C6 wireless facade
business policy or P4-C6 protocol semantics.

## Relationship To Other Layers

- `boards/t_display_p4/*`
  Own board truth and hardware arbitration.
- `platform/esp/idf_components/t_display_p4/*`
  Own display/touch/LVGL runtime.
- `platform/esp/idf_common/*`
  Consume the board contract, but should not duplicate pin truth here.
