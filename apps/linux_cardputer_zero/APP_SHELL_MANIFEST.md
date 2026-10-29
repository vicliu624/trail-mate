# App Shell Manifest: linux_cardputer_zero

## Role

Product app shell / target app shell.

## Target Family

Cardputer Zero portable Linux device.

## Renderer Family

Linux device renderer. The product package defaults to the Wayland runtime
inside the Cardputer Zero user session. The framebuffer/evdev executable exists
only as an explicit device-debug fallback, not as the APPLaunch product path.

The shared LVGL main-menu profile is not simulator-owned. Cardputer Zero selects
`make_cardputer_zero_profile()`, an independent focus carousel constrained for
the 320 x 170 keyboard device. Its page descriptors come from the production
catalog; the compact page set also includes Protocol Probe, Calculator, Calendar
and Geocaching. Calendar uses the native worker and shared page/reminder/Map
flow, including durable waypoint saving and selection through its native worker.
Geocaching supports local drafts through the shared Linux
background source; directory transport and signed publication remain incomplete.

## Board Facts

- board id: `cardputerzero`
- display: 320 x 170 logical pixels
- input: built-in keyboard, real key mapping still needs device sampling
- pointer/touch/trackball: not present in current board facts
- external module: M5Stack Cap LoRa-1262, completing both LoRa and GPS/GNSS
  capability for this target
- LoRa: SX1262 on `/dev/spidev0.1`, 500000 Hz, Reset=26, IRQ/DIO1=23,
  Busy=22, DIO2 RF switch enabled, DIO3 TCXO enabled
- GPS/GNSS: ATGM336H-6N@AT6668 on the Cap LoRa-1262 UART, NMEA 0183 4.1,
  default 115200 bps, with Cardputer Zero wiring `G15=GPS_RX` and
  `G14=GPS_TX`
- The `G15`/`G14` mapping is Cardputer Zero evidence and must not be inferred
  from the Cardputer-Adv `G13`/`G15` table.

## Linux Session Component Facts

- notifications: standard `org.freedesktop.Notifications` D-Bus provider
  supplied by the Cardputer Zero user session
- notification shell IPC: `$XDG_RUNTIME_DIR/cardputer-zero/notifyd.sock`,
  status/control only, never a notification creation path
- input method: Fcitx5 text input through the normal Linux frontend path
- IME display bridge: `cardputerzero-ui` Fcitx5 UI addon exports panel state to
  `$XDG_RUNTIME_DIR/cardputer-zero/ime-panel.sock`
- IME panel: `cardputer-zero-ime-panel` renders candidate/preedit state as a
  Wayland layer-shell projection and does not submit text
- expected session environment: `XMODIFIERS=@im=fcitx`,
  `SDL_IM_MODULE=fcitx`

## Build Entrypoint

- `builds/linux_cmake`

## Responsibilities

May:

- select the `cardputerzero` target profile
- select the Cardputer Zero device UX profile
- bind Cardputer Zero board facts into Linux runtime startup
- own Wayland APPLaunch packaging and launch details for this device
- own explicit framebuffer/evdev debug fallback details for this device
- own future Trail Mate Linux SX1262 packet-radio wiring for the documented
  Cardputer Zero LoRa endpoint
- own future Trail Mate Linux GPS serial wiring for the documented Cap
  LoRa-1262 GNSS endpoint
- route application notifications through the standard freedesktop notification
  contract
- respect the Cardputer Zero Fcitx5 user-session boundary for text input

Must not:

- identify itself as a Linux simulator
- depend on simulator demo data as product truth
- define protocol, chat, map, or storage semantics
- absorb shared Linux runtime code that belongs in `platform/linux/common`
- hide missing real-device hardware integration behind simulator naming
- use Cardputer Zero notifyd shell IPC to create notifications
- embed or fork the Cardputer Zero notifyd store, toast, or notification-center
  implementation
- turn the IME panel socket into a text submission path
- embed the Cardputer Zero Fcitx5 addon, panel renderer, input method engine,
  dictionary, or candidate selection policy inside Trail Mate
- adopt the external Meshtastic daemon implementation as the Trail Mate LoRa or
  GPS runtime

## Thin App Shell Entrypoint Declaration

```text
trail_mate_linux_cardputer_zero_start(target_profile)
```

## Current Status

Device app shell baseline. This establishes the product target, board facts, UX
selection, CMake wiring, Linux session contracts, page manifest alignment, the
Pager-derived main-menu profile guard, and shared-LVGL 320 x 170 screenshot
evidence for the compact page set. The installed `0.1.38-alpha` APPLaunch
package defaults to Wayland and retains `trailmate-cardputer-zero-fbdev` only as
an explicit framebuffer debug fallback. Real Wayland session startup,
Fcitx5/notifyd session validation, hardware framebuffer debug fallback, and
live interaction remain hardware-closure slices.
