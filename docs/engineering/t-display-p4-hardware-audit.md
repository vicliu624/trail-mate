# T-Display-P4 V1 hardware audit

Reviewed on 2026-10-15 for PR #104. Both TFT and AMOLED firmware targets use
the same V1 board wiring. This is a source audit and host regression report;
physical acceptance results must be recorded separately.

## Probe transmit recovery (2026-10-30)

The shared IDF LoRa UI port now reports a completed packet transmission as
successful only if restarting RX also succeeds. Previously it discarded the
restart result and could report success after a failed return to reception.
An exact-source host test covers successful TX/RX, RX restart failure, rejected
start, IRQ timeout and elapsed timeout across clock wrap. The subsequent
receive-path test additionally rejects RX_DONE combined with CRC/header/timeout
errors before payload reads, verifies no RX restart on normal preamble/sync/header
progress, and rejects publication when RX restoration fails. This supplements
the LR2021 driver's damaged-packet checks at the shared IDF port boundary.
The native FM/TV boundary suite passed 18/18, and the TFT LR2021 ESP-IDF firmware
build passed. The current 5,374,592-byte application has SHA-256
`c472abc0e6b3ac4675a34db6a8c3644f4c15a1f384d4b4b3421a489b2559f918`.
No flashing or physical RF acceptance was performed for this change. The later
FSK/FM handoff build below supersedes this binary.

## Saved FSK mode and continuous FM handoff (2026-10-30)

The LR2021 backend commits all configurable FSK parameters only after successful
configuration and RX startup. Continuous FM can now preserve that committed
profile: it stages an actual LoRa PHY for the existing IQ engine, then performs
a cold reset and restores FSK frequency, bitrate, deviation, bandwidth, power,
preamble, sync word, CRC and the prior RX/standby state under the physical mutex.
Staging/capture failure still runs restoration; restoration failure leaves the
driver offline. The original configuration cannot be overwritten by a failed
reconfiguration. This does not change finite Probe IQ's LoRa-only contract.

Exact production configuration/staging/restoration bodies passed fault-injection
tests, including RX/standby, all saved fields, invalid profile, staging failure,
capture failure and restoration failures. The native suite passed 19/19, the
existing IQ contracts passed 8/8, and the TFT LR2021 ESP-IDF build completed.
Current application: 5,379,328 bytes, SHA-256
`3e30912fcfcfb8fad9a3d1c400f082aceef8b902e6cd81f259e38de18b5b3a6e`.
No board was accessed or flashed. Physical mode restoration and FM listening
remain acceptance gates.

## LR2021 variant follow-up (2026-10-29)

The user confirmed TFT + LR2021. The follow-up adds explicit
`t_display_p4_tft_lr2021` and `t_display_p4_amoled_lr2021` targets alongside the
original SX1262 targets. The TFT LR2021 firmware compiled and linked with
ESP-IDF 5.5.4: application size 5,351,616 bytes in a 6 MiB app partition, with
15 percent remaining. This is build evidence, not physical RF acceptance.

The native LR2021 backend uses the pinned RadioLib 7.8.1 implementation and
the same shared configuration transaction as Linux. Mesh, Probe, Walkie and
quick controls resolve the same selected singleton. IRQ conversion preserves
the existing IDF logical bits; RX/TX/FIFO and error bits are not interchangeable
between chips. The board owns one recoverable radio reference with a wrap-safe
retry delay, while foreground tools retain their own references. A transport
failure marks the driver offline, invalidates configuration and asserts reset;
Walkie's task also clears its active/PTT/monitor state if the radio goes offline.

Packet reads reject hardware length/address errors as well as CRC/header
damage. The original TFT SX1262 target also compiled successfully. Object-symbol
checks verified the board, Probe runtime, Walkie runtime and quick controls use
LR2021 in the new target and SX1262 in the original target. Host tests cover IRQ
translation, board-reference recovery and both quick-control selections; these
checks do not replace physical packet or audio tests.

Board setup follows the [current pinned V1 vendor reference](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/d25dba8623c5fa6475b756e5a601f3b55862c479/src/device/t_display_p4/v1/driver.cpp):
3.0 V TCXO, DIO6 RX-HF, DIO7 TX-HF, DIO8 LF path, DIO10 HF path and DIO11 IRQ.
RadioLib validates ROM 1.24. Chip RF-switch writes are checked individually;
the convenience `setRfSwitchTable` API discards those results. The software
BUSY deadline also terminates the dependency's calibration wait on a stuck pin.
Large SPI scratch buffers live in internal DMA storage rather than task stacks.

[LR2021-SDR](https://github.com/SAMS0N1TE/lr2021-sdr) provides a portable raw-IQ
access path and an ESP32-P4 bridge reference. Its board-specific bus performance
does not establish performance on CM0. The FM decoder and frequency-range
limitations, SPI throughput prerequisite, ownership/restore requirements and
source-license distinction are recorded in the
[CM0 SDR investigation](t-display-cm0-hardware-integration.md#lr2021-sdr-investigation-2026-10-29).
Continuous raw-IQ acquisition and the broadcast FM receiver remain unimplemented. A portable
[mono FM DSP core](../../tests/broadcast_fm/README.md) now passes synthetic-IQ
tests for 250 ksample/s input and 48 kHz output, including full 75 kHz deviation,
de-emphasis, anti-alias filtering and discontinuity reset. It is not yet connected
to radio capture or speaker playback and does not establish target throughput.
The pinned MIT RX engine and bounded queue are now available through a separate
[RX contract build](../../tests/lr2021_iq/README.md). Its upstream transaction and
concurrent-queue tests pass on the host. The FM consumer validates block sizes,
rates, generations and sample indices before feeding the decoder.
The P4 driver now implements a finite 256-pair IQ adapter with real singleton
SPI, ROM preflight, SRAM verification and prior LoRa RX/standby restoration.
It rejects capture unless radio tasks are already paused, the caller holds a
radio reference, power is enabled and no FSK/TX is active. Output stays invalid
on any capture/recovery failure; recovery failure takes the radio offline.
Protocol Probe now exposes IQ Snapshot on the native P4 LR2021 target only,
through its button and `I` key. A worker captures the current mesh PHY, restores
and releases the radio before publishing diagnostics. Scan/apply actions are
blocked while capture is active; page exit joins recovery before transitioning.
Results explicitly retain an unqualified sample rate when the finite route does
not establish it, and never count IQ statistics as protocol evidence. This
diagnostic remains physically unverified and unsuitable for continuous FM.
No streaming FIFO producer/consumer tasks are enabled.
The compiled packet backend and
the earlier SX1262 hardware audit do not prove those capabilities or physical
LR2021 receive/transmit operation.

## Reference baseline

- [LilyGO V1 device driver, pinned revision](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/bcca4c674659abab850eaade06afb3f87cfbe85b/src/device/t_display_p4/v1/driver.cpp).
- [LilyGO V1 pin definitions](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/bcca4c674659abab850eaade06afb3f87cfbe85b/src/device/t_display_p4/v1/config.h).
- [LilyGO keyboard expansion definitions](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/bcca4c674659abab850eaade06afb3f87cfbe85b/src/device/t_display_p4/v1/keyboard_expansion_config.h).
- The checked-in `platform/esp/third_party/lilygo_cpp_bus_driver` submodule
  remains the keyboard's protocol implementation.
- [ESP-IDF MIPI DSI documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/lcd/dsi_lcd.html).
- AK09916 register sequence: ST1 at `0x10`, six XYZ bytes, temperature at
  `0x17`, and ST2 at `0x18`. Read through ST2 to release the data latch.

## Coverage and changes

| Hardware/path | Reviewed integration and outcome |
| --- | --- |
| Shared 3.3 V and XL9535 | Preserve active-low IO0 throughout boot. Read all physical output/configuration latches before modifying them. Commit cache changes only after successful I2C writes; invalidate readiness after uncertain transfers. |
| USB PHY power | IO10 is USB_PHY_POWER_EN, active HIGH. Corrected the opposite polarity. The legacy `p4_vcca` field name is documented to avoid confusing this with an internal P4 voltage rail. |
| Audio power | IO6 is AUDIO_POWER_EN, active HIGH. The legacy `power_5v` name is documented. The rail stays stable while the board owns ES8311. |
| DSI display | LDO3 corrected from 1830 mV to the official 2500 mV. Keep LDO4 independent at 3300 mV for GPIO39–48. Both panel timings, lane count, brightness backends and landscape rotation were compared with the board configuration. |
| Display/touch reset | Preload asserted reset before changing pin direction, then check reset release. Replace redundant long reset toggles with a deterministic low/high sequence. |
| HI8561 / GT9895 touch | SYS I2C address/protocol and AMOLED coordinate scaling reviewed. Register the LVGL input device under the display lock after the LVGL task starts. Touch transactions remain under the managed SYS I2C lock. |
| Keyboard electrical setup | GPIO46 SDA / GPIO45 SCL, XL9555 `0x20`, reset IO6, TCA8418 `0x34`, 10×7 matrix, GPIO47 backlight. Preload reset LOW before enabling output. LDO4 must be 3300 mV before accessing GPIO45/46. |
| Keyboard input | Preserve separate press/release samples through LVGL, native Enter clicks, Tab traversal, held-key repeat, page KEY callbacks and application shortcuts. Track physical Shift/Fn release, ignore duplicate presses, recover from FIFO overflow and persistent I2C failure, and retry a missing optional keyboard. Respect modal focus groups and page-level ESC navigation before app exit. |
| I2C allocation | SYS uses LP I2C0 on GPIO7/8 with the LP clock-source selector; EXT uses HP I2C1 on GPIO20/21; keyboard uses HP I2C0 on GPIO46/45. This matches LilyGO's current three-bus allocation. The keyboard's XL9555 owns the bus and its TCA8418 borrows it; failed probes explicitly remove both device handles before deleting the owning bus. |
| SD card | SDMMC slot0, 4-bit GPIO39–44, separate from C6 slot1. P4 now selects the same shared SDMMC transfer policy as Wio L2: 32-sector direct batches, eight-sector aligned bounce buffer and 16 KiB map-read chunks. FAT16/FAT32/exFAT round trips and boundary/error paths are covered with real SdFat. |
| USB mass storage | Existing storage ownership handoff and shared SDMMC host registry were reviewed. App storage and raw USB ownership must remain exclusive; releasing SD slot0 must not force-deinitialize C6 slot1. |
| C6 companion | GPIO14–19 SDIO wiring and XL9535 IO13/IO14 boot/enable sequence reviewed. C6 transport uses slot1 and its own lifecycle/refcounts. Network/BT behavior and download-mode timing still need device acceptance. |
| ES8311 audio | GPIO12 BCLK, 13 MCLK, 9 WS, 10 DOUT and 11 DIN match the board. Add bounded initialization retry and reverse-order cleanup of all partial codec/I2S allocations. Serialize lazy mutex initialization. Apply `CodecCompat::setGain()` to hardware with owner checks; fail a session if initial gain/volume configuration fails. Mono/stereo conversion uses runtime-owned scratch storage. |
| L76K GNSS | GPIO23 is host TX and GPIO22 host RX. Auto baud can retry factory 9600 after a full startup grace period without valid NMEA, including silence/garbage rather than only all-zero bytes. Explicit baud and a previously working receiver are preserved. Use 1 Hz at 9600 to avoid overwhelming the UART; keep 5 Hz at the normal 115200 setting. |
| ICM20948 / AK09916 | Read nine bytes through ST2, rather than eight bytes ending at temperature. Check overflow in ST2. Reprobe stale sensors even when the first sample never arrived, and use wrap-safe retry deadlines. Raw planar heading is still not a calibrated, tilt-compensated compass. |
| AW86224 haptic | RTP mode, GO, sample rate and status register sequence compared with the vendor driver. Clear cached readiness after failed playback/stop transfers so a subsequent trigger reinitializes the device. Waveform tuning remains a physical acceptance item. |
| PCF8563 RTC | Validate BCD digits as well as calendar ranges. Reject stopped clocks, read failures and low-voltage time. Existing write path restarts the oscillator and restores control after failed date writes. |
| BQ27220 battery gauge | SYS I2C `0x55`, little-endian word access, signed current and readiness handling reviewed. Charging indication and calibration require battery/device measurements. |
| SX1262 LoRa | SPI2 GPIO2/4/3, NSS24, BUSY6 and expander reset/DIO1/RF switch match the reference. Expander IO10–17 names are decimal pin labels for port1, not invalid linear indices. The existing pin decoder handles them explicitly. RF performance requires two-device testing. |
| Ethernet | IP101 is not initialized by this product. Keep XL9535 IO5 reset asserted until a real RMII owner configures the clock/pins. Previously it was released without an owner. |
| Camera / expansion radios | OV2710/SGM38121 camera, QMI8658/QMC6309 alternative motion hardware, and keyboard T-MixRF radios/NFC are outside the implemented product backend. LR2021 packet support is described in the follow-up above and still requires physical acceptance. An official driver supporting a part does not establish verified support in Trail Mate. |
| Power button/sleep | GPIO35 and existing bounded monitoring reviewed. Deep-sleep wake and actual off-state current need measurement; no unverified wake source is introduced. |

## Automated evidence

### Device follow-up on 2026-10-16

The TFT device was flashed from the current branch through its ESP32-P4 ROM
USB identity. Display and touch were confirmed working on the device. Brightness
and volume sliders initially returned to old values because native IDF did not
pump quick-control requests; adding that pump also exposed a missing C6 user
Wi-Fi switch implementation that the unused path had previously hidden from
the linker. The control pump and guarded Wi-Fi switch are now tested, and the
user confirmed slider operation with display/touch still working.

The keyboard did not acknowledge XL9555 address `0x20` using either the old
software bus or the official hardware-bus allocation. Hardware I2C alone is
therefore not a demonstrated keyboard fix. The accessory data connector was
reseated; factory-firmware keyboard operation has not been established.
Startup diagnostics now report both controller addresses, alternate expander
addresses, and SDA/SCL/INT levels. The official keyboard schematic separates
5 V backlight power from 3.3 V controller power, so a lit backlight does not
establish controller supply or data-bus continuity.

Absent-accessory retries no longer wait 50 ms for an already powered I/O rail
inside the LVGL timer. The initial short-probe change used a 5 ms SDK timeout;
the 2026-10-28 follow-up below corrects its RTOS tick truncation. Diagnostic
summaries retain their 30-second rate limit and hot-plug checks retain their
five-second cadence. Input initialization, hardware ACKs, actual Chat typing,
and resource behavior during physical connection changes remain separate
acceptance criteria.

### Keyboard discovery follow-up on 2026-10-28

The effective TFT SDK configuration uses `CONFIG_FREERTOS_HZ=100`, including
the retained configuration from the October 16 device session. ESP-IDF
[`i2c_master_probe()`](https://github.com/espressif/esp-idf/blob/v5.5.4/components/esp_driver_i2c/i2c_master.c)
converts its millisecond argument through `pdMS_TO_TICKS()` and waits for an
interrupt-driven completion using that tick count. A 5 ms argument becomes
zero ticks at 100 Hz and can time out before the completion interrupt is
delivered, even when an accessory is present. Commit `40e460bf9` introduced
that timeout while reducing absent-accessory stalls.

The probe now waits for 20 ms, with a compile-time check that this provides at
least two ticks. Its result remains an `esp_err_t`: diagnostics distinguish
`ESP_ERR_NOT_FOUND` (address NACK) from `ESP_ERR_TIMEOUT` and other SDK errors.
They also print the actual wait in milliseconds and ticks. An XL9555 timeout
skips the seven alternate-address probes so a stuck bus does not multiply the
UI-task wait. Normal absent-device retries and vendor protocol ownership are
unchanged.

The production adapter is tested with real LVGL at both 100 Hz and 1000 Hz
fake RTOS configurations. The hardware boundary now models IDF's tick
conversion instead of assuming that a millisecond equals a tick. With the
old production probe unchanged, the 100 Hz test failed at keyboard readiness
after attaching the accessory. The corrected probe passes the same input,
focus, repeat, modifier, hot-plug and recovery contracts. A separate timeout
case checks that only XL9555 and TCA8418 are probed, then verifies recovery.
The complete `t_display_p4_tft` firmware was built with ESP-IDF 5.5.4 and exited
successfully. The application is 5,277,648 bytes and fits its 6 MiB partition.
This local build covers the TFT target; it is not physical keyboard acceptance.

The wiring and reset sequence were compared again with the official
[V1 configuration](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/d25dba8623c5fa6475b756e5a601f3b55862c479/src/device/t_display_p4/v1/keyboard_expansion_config.h)
and [keyboard initialization](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/d25dba8623c5fa6475b756e5a601f3b55862c479/src/device/t_display_p4/driver.cpp).
SDA46/SCL45, HP I2C0, XL9555 `0x20`, reset IO6, TCA8418 `0x34` and the 10-by-7
scan window agree. No replacement keyboard protocol driver or pin reassignment
is required by this comparison.

No physical device was connected for this follow-up. This fixes a reproducible
discovery regression; it does not establish that the older software-bus
failure or the original hardware issue is resolved. The next device test must
record the SDK probe result, the `P4 keyboard ready` message and actual Chat
input separately. Backlight illumination remains independent evidence.

`tests/p4_keyboard` compiles the production keyboard adapter against real LVGL
9.5 and fake hardware boundaries. It covers click edges, focus, repeat, stale
groups, page handlers, modifier chords, modal ownership and recovery.

`tests/p4_hardware` compiles the actual heading, audio, codec adapter and haptic
sources. Power/RTC/GNSS tests extract unchanged production functions and drive
observable register/SDK boundaries. Tests cover each partial audio setup failure,
retry backoff, cleanup, ownership, gain, chunked PCM, magnetometer latch/overflow,
RTC trust, power ordering, failed expander writes and GNSS policy/checksums.
They do not validate analog behavior, bus timing or task concurrency. The legacy
Windows MinGW host uses a single-threaded mutex boundary; Linux CI uses actual
standard-library mutexes.

CI also builds both complete P4 firmware variants. Cross-compiling individual
objects locally checks SDK declarations, but is not a substitute for linking the
complete firmware or testing a board.

Follow-up synchronization fixes serialize shared LDO4 acquisition/adjustment and
publish board readiness atomically. On Linux CI, eight concurrent test workers
exercise 256 shared-LDO preparations and assert one acquisition with no overlapping
SDK calls. Display tests verify that initial rotation and brightness writes hold
the LVGL lock and that failed brightness writes preserve the cached value.
The rendered dashboard/lifecycle checks are recorded in the
[UI audit](t-display-p4-ui-audit.md).

## Independent FM integration status

FM is a separate application, not a Protocol Probe mode. The shared
`FmReceiver` service owns its station, session, PCM volume and failure cleanup.
The P4 codec backend now recognizes a dedicated FM owner and audio-focus kind.
It opens at 48 kHz with unity hardware volume because the receiver applies PCM
gain. Notification volume changes do not alter the active FM session. Host
tests exercise PCM stereo expansion, rejection of another owner's writes and
close attempts, music suspension/resumption, and subsequent Walkie ownership.
These tests use a fake codec transport; they do not prove physical speaker output.

The continuous receive adapter and standalone menu/page are still pending.
Do not mark FM supported from the existence of the codec owner or DSP tests.
The pinned upstream stream loop busy-polls and cancels through its sink; an
empty FIFO therefore also needs an independent bounded cancellation mechanism.
The current TFT LR2021 build enables idle-task watchdog checks on both cores
and uses `CONFIG_FREERTOS_HZ=100`. At 250 kpair/s IQ8, a nominal 1 KiB FIFO spans
about 2 ms, less than one 10 ms scheduler tick. A tick-delay polling adapter is
therefore unsuitable. Implement a checked, bounded producer scheduling and
cancellation path before connecting the service; preserve watchdog coverage.
Qualify actual RF tuning/passband and FIFO service latency on the board before
claiming reception across 87.5–108 MHz.

The user-selected first physical FM acceptance frequency is **100.8 MHz**
(`100800000` Hz). Record actual tuning success, continuous FIFO validity,
demodulated PCM, audible speaker output and stop/exit restoration at that
frequency. It is a test frequency, not evidence that a station was received.
The RX engine's local cancellation overlay now handles empty FIFO cancellation
and unpublished-tail discard; RTOS scheduling and physical passband validation
remain separate outstanding work.

## Physical acceptance to record

The optional keyboard's LVGL adapter remains registered during discovery retries.
The IDF helper now reports a keyboard to editors only while its controllers are
ready, so an absent or failed keyboard does not suppress the touch IME. A host
test exercises absent, attached, disconnected and missing-adapter states against
the production helper. An editor chooses its keyboard mode when it opens; reopen
the editor after changing the expansion keyboard connection.

The P4 adapter also forwards physical Space holds to active Walkie PTT, matching
Pager's input contract. It consumes those edges instead of clicking Monitor and
releases PTT on key-up, session/screen changes, FIFO overflow or persistent I2C
failure. Real-LVGL adapter tests exercise these cancellation paths; they do not
transmit RF. Walkie still uses the expansion keyboard for hold-to-talk input.

1. Cold boot each panel variant repeatedly with and without the keyboard. Verify
   image, touch corners/rotation, minimum/maximum brightness, USB enumeration,
   and absence of spurious resets when SD/C6 start.
2. Exercise keyboard quick taps, held Shift/Fn, Enter, Tab, navigation, nested
   chat back, settings dialogs and keyboard disconnect/reconnect. Check the
   backlight independently from input readiness.
3. Read maps and write tracks on FAT32/exFAT while C6 networking is active.
   Hand the card to USB MSC and back, confirm filesystem integrity and C6
   continuity, and record throughput/latency rather than inferred speedups.
4. Test microphone/speaker, Walkie/SSTV/call ownership, gain changes and recovery
   after a failed codec transfer. Confirm sample rate and clipping on hardware.
5. Start GNSS from factory 9600 and configured 115200, with Auto and explicit
   settings. Check valid NMEA before and after sleep/wake, cold acquisition and
   RTC synchronization without a network clock.
6. Rotate the board through a full turn and verify changing raw magnetometer
   samples; test temporary sensor failures. Verify haptic click strength, signed
   charging/discharging current, battery low-voltage RTC rejection and LoRa
   transmit/receive with a second device.
