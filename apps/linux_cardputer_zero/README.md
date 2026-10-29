# Cardputer Zero Linux App Shell

The current [production parity review](../../docs/engineering/cardputer-zero-maintenance.md)
records corrected stale wiring and outstanding shared Linux/compact UX gaps.
The compact shell now uses production catalog descriptors, includes Protocol
Probe, and services queued quick controls. Its target renderer is LVGL.

This app shell is the Trail Mate product route for the Cardputer Zero portable
Linux device. It is not the Linux simulator.

The runtime entry stays small: the shell validates
the `cardputerzero` target profile, consumes the `boards/cardputerzero` facts,
selects the `cardputer_compact` UX pack, and builds through
`builds/linux_cmake`. The device package now owns a Wayland APPLaunch product
entry and a separate explicit fbdev/evdev debug fallback; hardware validation
still has to happen on the real device session.

The shared LVGL main-menu profile for this target is Cardputer Zero specific:
`make_cardputer_zero_profile()` owns a compact focus carousel for the 320 x 170
display. It remains independent of the Pager's newer grid layout. Business
pages continue using shared page geometry and runtime ports.

Known hardware facts in this repo:

- display: 320 x 170 logical pixels
- input: built-in keyboard
- touch, pointer, and trackball: absent in current board facts
- external module: M5Stack Cap LoRa-1262, completing both LoRa and GPS/GNSS
  capability for this target
- LoRa: SX1262 on `/dev/spidev0.1`, 500000 Hz, Reset=26, IRQ/DIO1=23,
  Busy=22, DIO2 RF switch and DIO3 TCXO enabled
- GPS/GNSS: ATGM336H-6N@AT6668 on the Cap LoRa-1262 UART, NMEA 0183 4.1,
  default 115200 bps, with Cardputer Zero wiring `G15=GPS_RX` and
  `G14=GPS_TX`
- The `G15`/`G14` mapping is Cardputer Zero evidence and must not be inferred
  from the Cardputer-Adv `G13`/`G15` table.

Known Cardputer Zero Linux session facts in this shell:

- notifications are created through the standard
  `org.freedesktop.Notifications` D-Bus interface; the
  `$XDG_RUNTIME_DIR/cardputer-zero/notifyd.sock` channel is status/control only
- text entry uses the compositor's Wayland text-input-v3 frontend for Fcitx5
  commits, with UTF-8 XKB fallback for unhandled keys; physical session
  integration still requires device validation
- the Cardputer Zero IME addon/panel pair uses
  `$XDG_RUNTIME_DIR/cardputer-zero/ime-panel.sock` only to display preedit,
  candidates, and input-method state
- Trail Mate does not own the notification daemon implementation, Fcitx5 UI
  addon, IME panel renderer, input method engine, dictionaries, or text commit
  path
- the shared embedded touch/pinyin IME is not the Cardputer Zero Linux input
  method strategy
- Cardputer Zero business pages are wired through runtime/presentation ports;
  this target opts out of legacy chat-delivery and legacy presentation source
  compilation, and the Linux CMake helper withholds the legacy-adapter include
  root for this target, so `Legacy*` compatibility adapters cannot become the
  new product path
- Trail Mate must use its own Linux raw LoRa and GPS runtime paths for the Cap
  LoRa-1262; the external Meshtastic daemon package is hardware-reference
  evidence only, not the implementation owner

The simulator remains a separate development shell under `apps/linux_sim_shell`.

## Device Launch And Debian Package

The package route builds the real Cardputer Zero shell, not the screenshot tool
and not the simulator:

```bash
cd builds/linux_cmake
cmake --preset linux-cardputer-zero-release
cmake --build --preset linux-cardputer-zero-deb
```

On Windows/WSL, build from the helper instead so the package staging tree lives
on a Linux filesystem and does not inherit `/mnt/c` executable-bit pollution:

```bash
bash apps/linux_cardputer_zero/tools/build_cardputer_zero_deb.sh
```

The resulting package is written under `build/cardputer-zero-deb/`. The helper
builds through the Cardputer Zero Docker Compose builder, producing an actual
Docker `linux/arm64` Debian package while keeping build artifacts out of the
repository root. The first run builds the reusable
`trailmate-cardputer-zero-builder:bookworm-arm64` image and installs Debian
build dependencies into that image layer. Later package builds reuse the image
and the compose-owned CMake build volume, so they should not repeat `apt-get
install` unless the builder image is rebuilt, the APT mirror argument changes,
or the Docker cache is cleared.

The package helper still creates a filtered source snapshot under
`/tmp/trailmate-cardputer-zero-package`, excluding local object files, root
CMake state, and existing package outputs before copying that snapshot into the
builder container. This keeps Windows/WSL executable-bit behavior and accidental
root build artifacts out of the `.deb`.

The container package entrypoint validates the selected package version (by default
resolved from the CI tag or Changelog),
architecture, required runtime dependencies, APPLaunch paths, executable
permissions, AArch64 ELF metadata, and `ldd` resolution before copying the
`.deb` into `build/cardputer-zero-deb/`.

If Docker's Debian mirror path is unstable, override the builder image APT
mirror with `TRAIL_MATE_CARDPUTER_ZERO_APT_MIRROR`. The value should be the
Debian archive root, for example:

```bash
TRAIL_MATE_CARDPUTER_ZERO_APT_MIRROR=http://mirrors.tuna.tsinghua.edu.cn/debian \
  bash apps/linux_cardputer_zero/tools/build_cardputer_zero_deb.sh
```

Using an HTTP Debian mirror here avoids the base container's pre-bootstrap
certificate gap; package integrity still comes from APT's signed repository
metadata.

To force dependency refresh after changing mirrors or base images, remove or
rebuild the builder image:

```bash
TRAIL_MATE_CARDPUTER_ZERO_APT_MIRROR=http://mirrors.tuna.tsinghua.edu.cn/debian \
  docker compose \
  -f apps/linux_cardputer_zero/tools/compose.cardputer-zero-builder.yml \
  -p trailmate-cardputer-zero-package \
  build --no-cache deb-builder
```

The package installs the Cardputer Zero APPLaunch layout:

- `/usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero`
- `/usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero-fbdev`
- `/usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero-applaunch`
- `/usr/share/APPLaunch/applications/trailmate.desktop`
- `/usr/share/APPLaunch/share/images/trailmate-cardputer-zero.png`

The default APPLaunch path is Wayland. In `auto` mode the wrapper starts
`trailmate-cardputer-zero` only when `WAYLAND_DISPLAY` is present, and exits
with a diagnostic instead of silently falling back to framebuffer when no
Wayland session exists. The fbdev binary is reached only by setting
`TRAIL_MATE_DISPLAY_BACKEND=fbdev` or another explicit framebuffer alias; that
debug path honors `APPLAUNCH_LINUX_FBDEV_DEVICE` and
`APPLAUNCH_LINUX_KEYBOARD_DEVICE`, detects the ST7789V framebuffer from
`/proc/fb`, and falls back to `/dev/fb1` plus the documented Cardputer Zero
keyboard by-path event device. Set `TRAIL_MATE_FRAMEBUFFER=/dev/fbN` or
`TRAIL_MATE_INPUT_DEVICE=/dev/input/eventN` only for manual framebuffer
debugging outside the product Wayland session.

The APPLaunch wrapper also owns the Cardputer Zero Cap LoRa-1262 runtime
defaults. Unless explicitly overridden by the environment, it launches in real
mesh mode with:

- `TRAIL_MATE_RUNTIME_MODE=mesh`
- `TRAIL_MATE_LORA_SPI=/dev/spidev0.1`
- `TRAIL_MATE_LORA_GPIOCHIP=/dev/gpiochip0`
- `TRAIL_MATE_LORA_RESET_GPIO=26`
- `TRAIL_MATE_LORA_BUSY_GPIO=22`
- `TRAIL_MATE_LORA_IRQ_GPIO=23`
- `TRAIL_MATE_LORA_SPI_HZ=500000`
- `TRAIL_MATE_LORA_DIO2_RF_SWITCH=1`
- `TRAIL_MATE_LORA_DIO3_TCXO_1V8=1`
- `TRAIL_MATE_GPS_BAUD=115200`
- `TRAIL_MATE_GPS_AUTO_SERIAL=1`
- `TRAIL_MATE_GPS_DEVICE_CANDIDATES=/dev/serial0:/dev/ttyAMA1:/dev/ttyAMA0:/dev/ttyS0:/dev/ttyS1`
- `TRAIL_MATE_DESKTOP_NOTIFICATIONS=freedesktop`

`TRAIL_MATE_GPS_DEVICE` is reserved for an explicit user override. When it is
unset, the resident GPS service probes `TRAIL_MATE_GPS_DEVICE_CANDIDATES` and
fails over to the next existing candidate when a serial endpoint opens but no
NMEA bytes arrive. Serial aliases are de-duplicated after path canonicalization,
so `/dev/serial0` and its `/dev/tty*` target do not create a false failover
loop. The app launcher therefore publishes candidates rather than hard-selecting
`/dev/serial0`.

GPS and LoRa are resident services in this product shell. The app service loop
ticks the Cap LoRa-1262 radio path and the GPS serial/NMEA path even when the
Chat, Contacts, Map, or Sky Plot page is not currently open. Pages consume
runtime state and send commands; they do not own radio polling or GPS serial
reads.

Cardputer Zero Settings follows the same ownership boundary. Trail Mate exposes
application settings that its runtime can apply. Linux session brightness and
screen timeout belong to the operating system or desktop environment and are
therefore not shown in the application Settings page. For GPS, the Linux runtime
currently exposes the GPS enable switch, receiver baud selection, and
diagnostics; ESP-only receiver initialization policy items are not shown on
Cardputer Zero until the Linux runtime owns real receiver-command behavior for
them.

## LoRa And GPS Hardware Checks

Install the package on Cardputer Zero:

```bash
sudo apt install ./trailmate-cardputer-zero_1.0.0_arm64.deb
```

Check the installed package metadata and APPLaunch entry:

```bash
dpkg -s trailmate-cardputer-zero | sed -n '/^Package:/,/^Description:/p'
dpkg -L trailmate-cardputer-zero
sed -n '1,140p' /usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero-applaunch
cat /usr/share/APPLaunch/applications/trailmate.desktop
```

Before starting Trail Mate, confirm the hardware nodes exist:

```bash
ls -l /dev/spidev0.1 /dev/gpiochip0
ls -l /dev/serial0 /dev/ttyAMA1 /dev/ttyAMA0 /dev/ttyS0 /dev/ttyS1 2>/dev/null || true
cat /proc/cmdline | tr ' ' '\n' | grep '^console=' || true
pinctrl get 14
pinctrl get 15
systemctl status serial-getty@ttyS0.service --no-pager || true
```

If `/dev/spidev0.1` is missing, LoRa cannot be brought up by Trail Mate. If all
serial paths are missing, GPS cannot be read by Trail Mate.

`console=ttyS0,115200` or `console=serial0,115200` means the OS is still using
the GPS UART as a serial console. That belongs in the Cardputer Zero OS profile,
not the Trail Mate package. Re-run the OS profile installer or manually remove
those console tokens, then reboot.

To watch the app launch logs directly from a terminal:

```bash
WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0} \
  /usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero-applaunch \
  2>&1 | tee /tmp/trailmate-cardputer-zero.log
```

The product path requires Wayland. For explicit framebuffer debugging only:

```bash
TRAIL_MATE_DISPLAY_BACKEND=fbdev \
  /usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero-applaunch \
  2>&1 | tee /tmp/trailmate-cardputer-zero-fbdev.log
```

Runtime packet logs are written under the Trail Mate settings root. By default
that is `$HOME/.trailmate_cardputer_zero/logs/`. If `TRAIL_MATE_SETTINGS_ROOT`
is set, use that root instead.

Watch LoRa packet logs:

```bash
tail -F "${TRAIL_MATE_SETTINGS_ROOT:-$HOME/.trailmate_cardputer_zero}/logs/lora.log"
```

Useful LoRa signs:

- `LoRa raw TX`, `Meshtastic raw TX`, or protocol-specific TX lines mean the
  app attempted radio transmission.
- `LoRa raw RX`, `Meshtastic raw RX`, or protocol-specific RX lines mean the
  SX1262 delivered received bytes to Trail Mate.
- `LoRa radio degraded`, `SX1262 endpoint present; driver not online`,
  `open spidev failed`, `configure spidev failed`, or `probe failed` mean the
  runtime found the candidate path but could not bind or configure the radio.
- No `lora.log` after the app has been running for a few seconds usually means
  either `TRAIL_MATE_RUNTIME_MODE` is not `mesh`, `/dev/spidev0.1` is absent,
  or the app did not enter the Linux service loop.

Trigger a useful LoRa check from the UI by opening Contacts and running local
scan/discovery, then watch `lora.log` for TX/RX entries. Another node nearby
should cause RX entries when it broadcasts or replies.

Watch GPS/NMEA packet logs:

```bash
tail -F "${TRAIL_MATE_SETTINGS_ROOT:-$HOME/.trailmate_cardputer_zero}/logs/gps.log"
```

Useful GPS signs:

- `GPS source opened` means Trail Mate opened the selected serial source and
  configured its baud rate.
- `GPS source open failed` means the selected source path exists in config but
  could not be opened; check permissions, the `dialout` group, and the selected
  `/dev/tty*` path.
- `GPS serial waiting for NMEA` means the serial source is open but Trail Mate
  has not yet parsed a complete NMEA sentence.
- `GPS serial no traffic` means the currently opened auto-probed source
  produced no bytes within the failover window; Trail Mate will try the next
  candidate and log `next_path`.
- `NMEA sentence parsed` means Trail Mate read a complete NMEA sentence and its
  checksum was valid.
- `NMEA checksum failed` means UART bytes are arriving, but data integrity,
  baud rate, or wiring should be checked.
- No `gps.log` after the app has been running for a few seconds usually means
  the app has not reached the Linux service loop, GPS was disabled in Settings,
  every configured/auto-probed serial path is absent, or
  `TRAIL_MATE_SETTINGS_ROOT` points somewhere else. The Cardputer Zero GPS
  service runs independently of the Sky Plot or Map page being open.

Quick raw GPS byte check:

```bash
for dev in /dev/serial0 /dev/ttyAMA1 /dev/ttyAMA0 /dev/ttyS0 /dev/ttyS1; do
  [ -e "$dev" ] || continue
  echo "===== $dev ====="
  sudo stty -F "$dev" 115200 raw -echo -ixon -ixoff -crtscts 2>&1 || true
  timeout 8s sh -c "cat '$dev' | head -c 500" | od -An -tx1 -c
done
```

Healthy output contains `$GNGGA`, `$GNRMC`,
`$GPGSV`, `$GLGSV`, `$GAGSV`, or `$BDGSV` style NMEA sentences. A no-fix GPS
can still be healthy; the key first signal is valid NMEA traffic, then later
`FIX`/satellite values on the Sky Plot page.

Map tile diagnostics:

```bash
tail -F "${TRAIL_MATE_SETTINGS_ROOT:-$HOME/.trailmate_cardputer_zero}/logs/map.log"
find "${TRAIL_MATE_SD_ROOT:-$HOME/.trailmate_cardputer_zero/sd}/maps/base" -type f | head
sqlite3 "${TRAIL_MATE_SETTINGS_ROOT:-$HOME/.trailmate_cardputer_zero}/trailmate.sqlite" \
  "select source,z,x,y,status,http_status,bytes,last_error from map_tile_cache order by fetched_at desc limit 20;"
```

If `map.log` shows queued tiles followed by download failures, the Map page has
already handed the request to the Linux tile runtime. Check the network path
from the device itself:

```bash
getent ahosts tile.openstreetmap.org
curl -I -L --connect-timeout 5 https://tile.openstreetmap.org/7/100/54.png
```

Linux tile downloads use the system resolver by default. DoH is opt-in through
`TRAIL_MATE_MAP_DOH_URL` or `TRAIL_MATE_CURL_DOH_URL`; set one of them only
when that resolver is reachable from the device network. If a network prefers a
specific IP family, set `TRAIL_MATE_MAP_IP_RESOLVE=ipv4` or
`TRAIL_MATE_MAP_IP_RESOLVE=ipv6`. If the default public tile endpoint is not
reachable from the device network, configure a reachable template such as
`TRAIL_MATE_OSM_TILE_URL`.

Configure standard curl proxy variables (`HTTPS_PROXY`, `HTTP_PROXY`, or
`ALL_PROXY`) in the session or the documented environment files when needed.
The launcher preserves explicit settings and does not infer a proxy from a
machine-specific listening port.

Check the Cardputer Zero notification path:

```bash
notify-send "Trail Mate" "Notification daemon is working"
busctl --user call org.freedesktop.Notifications \
  /org/freedesktop/Notifications \
  org.freedesktop.Notifications GetServerInformation
journalctl --user -u cardputer-zero-notifyd -f
```

Incoming Trail Mate chat messages call the same freedesktop notification
interface when `TRAIL_MATE_DESKTOP_NOTIFICATIONS=freedesktop`. The
`$XDG_RUNTIME_DIR/cardputer-zero/notifyd.sock` channel remains status/control
only and is not used to create notifications.

This is a CPack device-install package. It intentionally does not claim the
full LoFiBox-Zero Debian source-package flow yet: there is no `debian/`
directory, no `dpkg-buildpackage` source package, no lintian/autopkgtest gate,
and no signed APT publication path for `amd64`/`arm64`. Those belong to a later
packaging-governance slice if Cardputer Zero needs LoFiBox-style preview APT
distribution.

## Screenshot Evidence

The current shared LVGL Cardputer compact manifest has fourteen product routes,
including Agenda.
The checked-in screenshot matrix covers ten earlier pages; Calculator,
Geocaching and Protocol Probe are also registered and covered by the current
maintenance tests. Agenda also has current registry/manifest test coverage.
Missing checked-in images are not evidence of missing routes. The command below
covers the existing thirteen capture selectors; it does not capture Agenda.
PC Link, SSTV, and SD Storage / USB Disk are intentionally excluded from this product menu. SD
Storage is the card-access/USB mass-storage entry, not Extensions.

```powershell
$targets = @(
  'dashboard',
  'chat',
  'contacts',
  'map',
  'sky_plot',
  'team',
  'tracker',
  'calculator',
  'geocaching',
  'energy_sweep', # Capture selector for the Protocol Probe page.
  'walkie',
  'extensions',
  'settings'
)
wsl bash -lc 'cd /mnt/c/Users/VicLi/Documents/Projects/trail-mate && rm -f docs/images/cardputerzero/screenshots/*.png'
foreach ($target in $targets) {
  wsl bash -lc "cd /mnt/c/Users/VicLi/Documents/Projects/trail-mate && timeout 40s env TRAIL_MATE_LORA_DISABLE=1 builds/linux_cmake/build/linux-cardputer-zero-debug/apps/linux_cardputer_zero/trailmate_linux_cardputer_zero_screenshot_capture /mnt/c/Users/VicLi/Documents/Projects/trail-mate/docs/images/cardputerzero/screenshots $target"
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
```

See `docs/targets/cardputerzero-screenshots.md` for the screenshot matrix,
including the Map page's compact zoom, center, base-layer, and contour controls,
and `docs/targets/cardputerzero-adaptation.md` for the current validation state
and remaining hardware/session closure.
