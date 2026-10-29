# Cardputer Zero Adaptation

Cardputer Zero is a portable Linux device target. It is not the Linux simulator
and must not inherit simulator naming, simulator demo state, or simulator UX as
product truth.

## Product Boundary

| Layer | Cardputer Zero owner |
| --- | --- |
| Target id | `cardputerzero` |
| Build entrypoint | `builds/linux_cmake` |
| App shell | `apps/linux_cardputer_zero` |
| Board facts | `boards/cardputerzero` |
| UX pack | `cardputer_compact` |
| UI profile | `cardputer_compact_ui` |
| Page manifest | `cardputer_compact_manifest` |
| Shared menu profile | `make_cardputer_zero_profile()` |

The current app shell establishes target identity and Linux session contracts.
It now has shared-LVGL per-page screenshot evidence for the Cardputer compact
path. It now also has a Wayland APPLaunch Debian package target with a separate
explicit fbdev/evdev debug fallback. It does not yet claim live Cardputer Zero
hardware/session closure.

## UX Baseline

Cardputer Zero uses the compact Pager visual language under a 320 x 170
keyboard-device constraint.

The shared LVGL menu profile keeps an independent compact focus carousel:

```text
make_cardputer_zero_profile()
  owns compact geometry independently of the Pager grid
  keeps PagerFocus menu behavior
  shrinks geometry for 320 x 170
  enables keyboard directional navigation
  hides memory statistics
```

Cardputer Zero page work must continue from this baseline. A custom Linux-panel
style preview, ASCII-only view, or simulator view is not acceptable evidence for
Cardputer Zero page adaptation.

The dense 320 x 170 page metrics are specified in
`docs/targets/cardputerzero-dense-ui.md`. Shared pages should consume
`ui::page_profile::current()` rather than branching on the Cardputer Zero target
inside business-page code.

## Screen Set

`CardputerCompactUxPack::buildScreens()` and `cardputer_compact_manifest` both
own the same fourteen product routes:

- Dashboard
- Chat
- Contacts
- Map
- Sky Plot
- Team
- Geocaching
- Calculator
- Tracker
- Protocol Probe
- Walkie Talkie
- Extensions
- Settings
- Agenda

Cardputer Zero intentionally does not expose PC Link, SSTV, or
SD Storage / USB Disk in its product menu. SD Storage is the USB
mass-storage card-access entry; it is not the same thing as Extensions.

Protocol Probe now uses the shared production catalog and Linux packet-radio
port, including foreground ownership and mesh restoration. A missing external
radio remains unavailable; menu registration does not imply hardware readiness.

The Cardputer Zero CMake test slice checks this manifest alignment directly.
The earlier maintenance capture tool completed the thirteen pre-Agenda pages in
successive sessions after resource-root configuration became idempotent for the
same path. Agenda is included in the current fourteen-page registry and manifest
assertions; that does not add a screenshot to the historical capture set. This is
host UI evidence; there is still no physical Cardputer Zero test device.
Cardputer Zero has no separate GPS-status product page. GNSS visual status is
the Sky Plot page; raw receiver/status facts are data feeding Sky Plot,
dashboard, tracker, team, and map behavior.

## Business Wiring Boundary

Cardputer Zero business pages must use runtime/presentation ports, not
`Legacy*` compatibility adapters. Legacy sources may remain in the repository
for older targets and historical burn-down, but they are not reusable assets for
this product path. The Cardputer Zero app shell opts out of legacy chat-delivery
and legacy presentation source compilation, and the shared Linux CMake helper
does not expose the `ui_legacy_adapters` include root unless an older target
explicitly opts into those legacy source groups.

This is a hard product boundary: Cardputer Zero fixes must land in runtime
models, runtime action sinks, presentation sources, protocol adapters, or the
target composition root. They must not hide missing behavior inside
compatibility layers or restore `Legacy*` as a new implementation owner.

Current non-legacy runtime wiring:

- Chat sends through `RuntimeChatActionSink` and reads through
  `ChatPresentationSource`.
- LoRa/mesh TX/RX is a resident Linux app service. The Cardputer Zero shell
  ticks the selected mesh adapter, then lets `ChatService::processIncoming()`
  and `TeamService::processIncoming()` drain events. Chat, Contacts, Team, and
  Map pages must not own radio polling or raw packet lifecycle.
- Map uses `RuntimeMapWorkspaceSource` and `RuntimeMapActionSink`.
- Sky Plot uses the shared GNSS sky-plot shell backed by runtime GNSS/GPS
  sources.
- Map uses live runtime map workspace state. Product startup must not inject
  simulator/demo/default GPS coordinates into Cardputer Zero Map; without a live
  GPS source or explicit test environment override, Map renders no-position
  state and waits for real data.
- Dashboard/status/footer/settings consume runtime device, mesh, GPS, and
  settings sources.
- GPS is a resident Linux app service. The shell ticks
  `platform::ui::gps::tick_service()` before tracker and team consumers read
  `platform::ui::gps` snapshots. Map, Sky Plot, Tracker, Team, Dashboard, and
  Settings must consume GPS state/diagnostics; they must not become serial/NMEA
  polling owners.
- Tracker polling writes points from `platform::ui::gps`.
- Team track sampling reads the Linux GPS runtime through
  `LinuxGpsTrackSource`.
- Raw SX1262 LoRa supports Meshtastic NodeInfo discovery actions through the
  real `LinuxRawLoraMeshAdapter`.

Settings ownership:

- Cardputer Zero Settings exposes only application-owned settings that the
  Linux runtime can apply.
- Screen brightness and screen timeout are Linux session/power-management
  policies. They are intentionally not application settings on Cardputer Zero
  and must not appear in Trail Mate Settings for this target.
- GPS receiver baud is exposed because it changes the Linux serial/NMEA runtime
  reopen behavior. ESP receiver initialization policies such as probe window,
  receiver profile, RXM init, GNSS init, and NMEA init are not exposed on
  Cardputer Zero unless the Linux GPS runtime implements real receiver-command
  ownership for them.

## Hardware Facts

Known facts currently recorded in the repository:

- display: 320 x 170 logical pixels
- input: built-in keyboard
- touch/pointer/trackball: absent in current board facts
- external module: M5Stack Cap LoRa-1262, completing both LoRa and GPS/GNSS
  capability for this target
- LoRa: SX1262 on `/dev/spidev0.1`
- SPI speed: 500000 Hz
- Reset GPIO: 26
- IRQ/DIO1 GPIO: 23
- Busy GPIO: 22
- Power GPIO: -1
- DIO2 RF switch: enabled
- DIO3 TCXO: enabled
- GPS/GNSS: ATGM336H-6N@AT6668 on the Cap LoRa-1262 UART
- GPS/GNSS protocol: NMEA 0183 4.1, default 115200 bps
- Cardputer Zero GPS pin mapping: `G15=GPS_RX`, `G14=GPS_TX`

The M5Stack Cap LoRa-1262 documentation is the module-level source for the
LoRa/GNSS capability, GPS UART transport, default baud rate, and NMEA protocol.
The `G15`/`G14` GPS mapping is Cardputer Zero external wiring evidence and must
not be inferred from the Cardputer-Adv `G13`/`G15` table or unrelated M5Stack
host-device pin tables.

The LoRa/GNSS pin reference is a hardware-fact input only. Trail Mate must not
adopt the external Meshtastic daemon implementation as the Cardputer Zero LoRa
or GPS runtime. Linux GPS serial runtime selection remains a runtime endpoint
configuration: `TRAIL_MATE_GPS_DEVICE` is an explicit user override, while
`TRAIL_MATE_GPS_DEVICE_CANDIDATES` supplies the auto-probe list used by the
resident GPS service. Candidate aliases are de-duplicated after path
canonicalization, so `/dev/serial0` and its `/dev/tty*` target do not create a
false failover loop. The application must not hard-select `/dev/serial0` in the
launcher because the OS serial alias can differ from the final Cap LoRa-1262 GPS
`/dev/tty*` mapping validated on hardware.

The OS image/profile owns UART availability. On Cardputer Zero, the OS profile
must release GPIO14/GPIO15 from the kernel serial console and `serial-getty`
before Trail Mate can consume NMEA bytes. Trail Mate diagnoses the selected or
auto-probed source, but it does not mutate boot console ownership from the app
package.

When GPIO14/GPIO15 are assigned to the UART alternate function (`TXD1`/`RXD1`),
they are not application-owned GPIO direction lines. Trail Mate must not force
them to ordinary input/output mode as a GPS recovery tactic; doing so would break
the UART transport. If `/dev/serial0` opens at the configured baud and no NMEA
bytes arrive, the next diagnosis belongs to OS pinmux evidence, physical wiring,
module power/output mode, or baud/source selection, not UI/page code.

## Linux Session Contracts

Notifications:

- application notifications are created through
  `org.freedesktop.Notifications`
- `$XDG_RUNTIME_DIR/cardputer-zero/notifyd.sock` is status/control only
- Trail Mate must not embed or fork the notifyd implementation
- Trail Mate must not use the notifyd shell IPC as a notification creation API

Text input:

- committed text comes through the normal Linux/Fcitx5 frontend path
- expected environment includes `XMODIFIERS=@im=fcitx` and
  `SDL_IM_MODULE=fcitx`
- `$XDG_RUNTIME_DIR/cardputer-zero/ime-panel.sock` is display-only JSON Lines
  state from the Fcitx5 UI addon to the panel
- Trail Mate must not turn the IME panel socket into a text submission path
- Trail Mate must not embed the Fcitx5 UI addon, panel renderer, input method
  engine, dictionaries, or candidate policy

## Current Validation

The Cardputer Zero Linux CMake preset validates:

- app-shell target identity
- board/UX target binding
- notification and Fcitx5 boundary declarations
- `cardputer_compact_manifest` route alignment
- Cardputer Zero menu profile selection through
  `TRAIL_MATE_CARDPUTER_ZERO_LINUX`
- Wayland product executable, explicit fbdev/evdev debug fallback, and Debian
  package metadata ownership
- package version `0.1.38-alpha`, APPLaunch path, Wayland display metadata,
  and required Wayland runtime dependencies
- Cap LoRa-1262 LoRa/GNSS board facts, including the `G15`/`G14` GPS UART
  wiring and product-profile GPS capability
- product live-only GPS fallback, so Cardputer Zero does not inherit simulator
  GPS demo coordinates unless an explicit test/demo environment requests them
- shared runtime `ui_status` wiring for menu/topbar status indicators; the
  Cardputer Zero product shell must not link the Linux no-op status stub

The menu-profile smoke test protects the existing Pager-derived main-menu
adaptation. It does not render pixels.

## Launch And Package

The Cardputer Zero product executable is `trailmate-cardputer-zero`. It is the
Wayland runtime. In the installed package it lives in the private app libdir and
is started through the APPLaunch wrapper:

- `/usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero`
- `/usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero-fbdev`
- `/usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero-applaunch`
- `/usr/share/APPLaunch/applications/trailmate.desktop`
- `/usr/share/APPLaunch/share/images/trailmate-cardputer-zero.png`

The APPLaunch wrapper's default `auto` mode starts the Wayland runtime only
when `WAYLAND_DISPLAY` is present. If no Wayland session exists, it exits with a
diagnostic instead of silently falling back to framebuffer. The desktop metadata
therefore records `X-Zero-Display=wayland`.

`trailmate-cardputer-zero-fbdev` is retained only for explicit framebuffer
debugging. It is reached by setting `TRAIL_MATE_DISPLAY_BACKEND=fbdev` or
another explicit framebuffer alias. That debug path honors
`APPLAUNCH_LINUX_FBDEV_DEVICE` and `APPLAUNCH_LINUX_KEYBOARD_DEVICE`, detects
the ST7789V framebuffer from `/proc/fb`, and falls back to `/dev/fb1` plus the
documented Cardputer Zero keyboard by-path event device.
`TRAIL_MATE_FRAMEBUFFER=/dev/fbN` and
`TRAIL_MATE_INPUT_DEVICE=/dev/input/eventN` remain manual debug overrides
outside the product Wayland session.

The Debian package route is owned by `apps/linux_cardputer_zero` and is built
from the Docker helper:

```bash
bash apps/linux_cardputer_zero/tools/build_cardputer_zero_deb.sh
```

The helper uses the Cardputer Zero Docker Compose builder. The builder image is
`trailmate-cardputer-zero-builder:bookworm-arm64` by default, runs as
`linux/arm64`, and owns the Debian build dependency installation layer. Package
builds reuse that image and the compose-owned CMake build volume instead of
installing build dependencies into a fresh container on every run.

The helper still creates a filtered source snapshot under
`/tmp/trailmate-cardputer-zero-package` and copies that snapshot into the
compose-launched builder container. This is deliberate: repository-root build
artifacts, stale object files, old `.deb` outputs, and Windows/WSL permission
projection must not become package input. The package output remains
`build/cardputer-zero-deb/`.

The container entrypoint builds an actual `arm64` `.deb`, validates package
version `0.1.38-alpha`, required dependencies, AArch64 ELF metadata and `ldd`
resolution, rejects unsafe permissions, verifies the APPLaunch layout, and then
copies the package out. The package route is not the screenshot-capture tool
and is not the Linux simulator.

The helper accepts `TRAIL_MATE_CARDPUTER_ZERO_APT_MIRROR` when Docker's default
Debian mirror path is unstable. The value is a Debian archive root such as
`http://mirrors.tuna.tsinghua.edu.cn/debian`; the helper derives the matching
`bookworm`, `bookworm-updates`, and `bookworm-security` sources inside the
temporary arm64 container. HTTP mirrors are acceptable for this bootstrap path
because APT still validates signed repository metadata and the generated `.deb`
is validated after packaging.

## Screenshot Evidence

The shared LVGL Cardputer compact UI path now has 320 x 170 screenshot evidence
for the ten Cardputer Zero product routes. See
`docs/targets/cardputerzero-screenshots.md`.

Captured pages:

- Dashboard
- Chat
- Contacts
- Map
- Sky Plot
- Team
- Tracker
- Walkie Talkie
- Extensions
- Settings

The screenshots are captured by the Cardputer Zero app shell's shared LVGL host,
through `ShellSession`, `CanvasLvglHost`, the `cardputer_compact` UX pack, and
the Pager-derived `make_cardputer_zero_profile()` menu profile. Screenshots
produced by ad hoc drawing scripts, simulator-only ASCII renderers, or
non-Cardputer UI shells do not satisfy this closure.

The current Sky Plot screenshot records the split page-identity fact:
Cardputer compact exposes distinct Map and Sky Plot route ids; Map renders the
map workspace, while Sky Plot renders the GNSS sky-plot projection backed by
runtime GNSS/GPS sources. The Map projection includes compact operation
controls for zoom, center-on-self, base-layer cycling, and contour toggling;
these actions are wired through the runtime map workspace and shared
`map_viewport` layer API, not through legacy presentation adapters. A screenshot
named `gps.png` is obsolete evidence for this product path.

The screenshot evidence and package build do not close live-device behavior.
Wayland APPLaunch startup, Fcitx5 committed text, freedesktop notification
delivery, Wayland keyboard mapping, explicit fbdev/evdev debug fallback,
modal/picker flows, and page layout under real interaction still need
Cardputer Zero session validation.

## Remaining Work

- validate Wayland APPLaunch startup on hardware
- validate Wayland keyboard mapping on hardware
- validate explicit fbdev/evdev debug fallback on hardware
- validate notifyd over the live D-Bus session
- validate Fcitx5 committed text in Trail Mate fields
- validate Trail Mate SX1262 runtime on the documented Linux endpoint
- validate Trail Mate GPS runtime on the documented Cap LoRa-1262 `G15`/`G14`
  UART wiring and final Linux serial device
- promote per-page screenshot capture into the regular Cardputer Zero
  validation flow
- revise each business page only where the Pager-derived 320 x 170 layout
  actually fails
- validate real LoRa discovery and NodeInfo exchange on hardware
