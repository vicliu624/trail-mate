# Configurable Linux LVGL application

The build entrypoint invokes this application shell. The shell composes the
existing Linux services, shared app catalog and LVGL UX pack. The SDL desktop
port owns the window, touch and keyboard input. Board profiles contain display
dimensions, orientation and fullscreen policy; the port contains no board IDs.

T-Display-CM0 selects `t_display_p4_touch`, including the P4 dashboard, menu,
page layouts, on-screen keyboard and bottom-edge back gesture. Pages use the
existing Linux platform services. The CM0 profile selects the LR2021 backend;
other Linux compositions may retain SX126x or disable radio support. A shared
coordinator restores mesh parameters after foreground RF tools release the chip.
The Linux-only RadioLib dependency is pinned separately from ESP builds.
Music uses the shared core player, rooted filesystem storage, SDL PCM output,
volume and audio focus. Other audio workflows, GNSS and optional peripheral integration remain in progress; see
`docs/engineering/t-display-cm0-hardware-integration.md` for the development ledger.

## Build and run

CM0 cross builds must use C++ headers and a link library matching its Raspbian
runtime, not generic Debian armhf defaults. See the
[target C++ ABI setup](../../docs/engineering/linux-armhf-cxx-abi.md) before
building a deployable CM0 deb. Use a fresh build directory when changing the ABI.

On Linux install Python 3, CMake, Ninja, a C++20 compiler, `libsdl2-dev` (SDL >= 2.0.18),
`libcurl4-openssl-dev`, `libsqlite3-dev`, `libssl-dev`, `libjpeg-dev`, `libpng-dev`, `libarchive-dev`,
`pkg-config` and `dpkg-dev`. Phone Bluetooth is intentionally disabled and deferred;
BlueZ and GIO are not required by the application.

```sh
cmake -S apps/linux_lvgl -B build/linux-lvgl -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DTRAIL_MATE_LINUX_DEVICE_CONFIG="$PWD/boards/t_display_cm0/package.cmake" \
  -DTRAIL_MATE_LINUX_DEB=ON
cmake --build build/linux-lvgl --parallel 4
ctest --test-dir build/linux-lvgl --output-on-failure
build/linux-lvgl/trailmate-linux --profile boards/t_display_cm0/device.conf
```

The application runs in the active X11 or Wayland desktop session. SDL selects
the available backend; `SDL_VIDEODRIVER=wayland` or `x11` can override it.
Fullscreen is the device profile default. Alt+F4 and window-close requests end
the loop and shut down application services. `--windowed` enables desktop
development. `--quit-after-ms 3000 --screenshot /tmp/trailmate.bmp` provides a
bounded capture; screenshots contain the unrotated logical UI.

The CM0 profile uses a 1232 x 568 landscape UI. The Raspberry Pi desktop already
rotates DSI-1, so application rotation defaults to zero. Set `rotation=90` in a
copied profile for a session exposing the native 568 x 1232 portrait output.
All four right-angle orientations are supported. Width,
height, title, fullscreen and rotation belong to the profile, not shared code.
The default `hardware_keyboard=auto` probes evdev key capabilities without
consuming input. Explicit true/false overrides detection. With it disabled,
touch editors keep the P4 on-screen keyboard; Alt+F4 still works.
The profile's `ux_pack` must match `TRAIL_MATE_LINUX_UX_PACK` at build time;
unlinked packs fail clearly instead of silently selecting another UI.

## Debian package

```sh
cpack --config build/linux-lvgl/CPackConfig.cmake -G DEB
sudo apt install ./trailmate-cm0_*.deb
```

Build on the target OS/architecture or with a matching cross toolchain and
sysroot. The vendor CM0 image is Raspberry Pi OS Trixie armhf; an amd64 desktop
build is for testing and must not be installed on CM0. CPack derives native
dependencies using `dpkg-shlibdeps`. The package installs its executable in
`/usr/bin`, the profile in `/usr/share/trailmate/devices`, the desktop entry in
`/usr/share/applications`, and the repository's existing `images/logo.png`
in `/usr/share/pixmaps`. It does not install an autostart service or
replace the vendor desktop/drivers.

To cross-build the CM0 armhf package from a Debian/Ubuntu amd64 host, install
`gcc-arm-linux-gnueabihf`, `g++-arm-linux-gnueabihf`, `curl`, `gnupg`,
`dpkg-dev`, `cmake` and `ninja-build`, then run:

```sh
bash apps/linux_lvgl/tools/build_cm0_deb.sh
```

The helper downloads signed Debian Trixie armhf development packages into an
isolated sysroot and does not install foreign packages into the host. Its
cache, output and parallelism can be set using `TRAIL_MATE_PACKAGE_WORK`,
`TRAIL_MATE_PACKAGE_OUT` and `TRAIL_MATE_PACKAGE_JOBS`. Output defaults to
`build/cm0-deb`. Cross-package runtime dependencies use Trixie's package names;
native builds derive their dependencies with `dpkg-shlibdeps`.

For another Linux device, provide a new profile and desktop entry, and select
its supported UX pack in the application composition. Keep peripheral drivers
behind the existing Linux runtime ports.

## Storage and sensor configuration

This desktop composition opts into XDG application paths: settings/state under
`$XDG_STATE_HOME/trailmate`, user data under `$XDG_DATA_HOME/trailmate`, and cache
under `$XDG_CACHE_HOME/trailmate`, with the usual `$HOME` fallbacks. The profile's
optional absolute `state_root`, `data_root`, and `cache_root` override those
defaults. Existing `TRAIL_MATE_SETTINGS_ROOT`, `TRAIL_MATE_SD_ROOT`, and
`TRAIL_MATE_CACHE_ROOT` environment overrides take precedence. Legacy Linux
compositions keep their previous locations until they explicitly opt in.

The music library uses the logical SD root and existing core Music/playlist
formats. Audio targets SDL's default output rather than a board-specific ALSA
card number. The application shuts down the player before SDL/LVGL teardown.

Native alerts use a worker and the same media volume/focus state as music.
Saved speaker volume is restored before incoming messages are handled; zero
mutes both music gain and alerts. A notification temporarily interrupts music
and yields to an existing call/voice/RF audio owner. Capture uses a bounded
one-second PCM ring and opens the default microphone only on explicit `begin`.
SSTV uses this capture port and the shared receive core. Walkie uses native
Codec2/WT v2 GFSK sessions with exclusive radio/audio ownership. Other voice/call
integration remains incomplete.

IMU support uses standard Linux IIO accelerometer/gyroscope/temperature channels,
including their scale and offset attributes. Configure `imu_expected`,
`imu_iio_root`, optional `imu_iio_device`, `imu_mount_rotation` and
`orientation_policy` in the device profile. `sensor_landscape_only` accepts
debounced landscape flips and ignores portrait orientations. Missing or failed
sensor channels remain not ready; no absolute compass heading is synthesized.

GNSS configuration uses `gps_protocol=nmea|nordic_slm`, optional absolute
`gps_device`, `gps_baud`, `gps_modem_lines` and `gps_gnss_only_setup`. The CM0
baseline has no fitted GNSS/IMU expansion: it locks landscape orientation,
does not expect an IMU, and does not claim a modem power line or issue SLM
commands. Enable expansion settings only in a user-owned profile after fitting
the hardware. A serial
device can be an exact stable path or a filename pattern in one absolute
directory, such as `/dev/serial/by-id/usb-receiver_*-if00`. A pattern must match
exactly one character device; missing or ambiguous matches remain unavailable
and are retried at the serial-open boundary. Directory patterns are unsupported,
and each scan is bounded to 256 entries. With multiple matching receivers,
configure the full by-id path or a dedicated udev alias. Existing
`TRAIL_MATE_GPS_DEVICE`/NMEA-file overrides take precedence over the profile
source; saved receiver-baud settings and environment overrides remain supported.
Passive NMEA never sends AT commands. SLM is an explicit device choice and
requires command acknowledgements before reporting ready. It projects `#XGPS`
positions into the shared GPS service without fabricating satellites or HDOP.
The GNSS-only setup flag disables cellular mode; keep it false for a modem
whose LTE/GNSS coexistence is managed elsewhere. Disabling GPS requests a
best-effort stop and releases the serial port.

Automatic display power control is an explicit profile capability:
`screen_power=wayland` uses the compositor's `wlopm` interface, while the default
`screen_power=none` leaves it unavailable. A single output is discovered
automatically; with multiple outputs, set `screen_power_output` to its advertised
name. The shared timeout, screen saver and sleep inhibitors run through the
Linux screen service. Wake input is consumed before reaching pages or editors;
SPACE confirms keyboard resume, and a subsequent upward swipe confirms touch
resume. A lost output or failed power request releases the UI's sleep state.
Shutdown attempts to restore outputs the application turned off. This does not
provide brightness control. CM0 ships with automatic power cycling disabled
pending physical confirmation of stock-panel recovery. See
[output power](../../docs/engineering/linux-output-power.md).

Wireless interface discovery accepts `TRAIL_MATE_WIFI_INTERFACE` as an explicit
Linux interface name. Without it, exactly one wireless interface must be present;
multiple adapters are unavailable until selected explicitly. The selection is
validated against the wireless class marker, so a missing, renamed or wired
interface does not silently fall back to another adapter. No `wlan0` assumption
is required. This selector affects the app's Wi-Fi controls; ordinary IP traffic
continues to use the operating system's routes.

An explicit Wi-Fi enable or automatic connect request can try saved profiles in
priority order on the worker. Existing desktop connections are retained. Manual
connect/disconnect/disable and interface changes cancel the cycle; exhaustion
stops app retries without disabling the system-wide NetworkManager radio.
Merely saving profiles or starting the app does not initiate that cycle.

Brightness uses a background Linux backlight adapter. `backlight_root` defaults
to `/sys/class/backlight`; optional `backlight_device` selects an absolute class
device path. One valid device is auto-selected; multiple devices require explicit
selection. Settings/quick controls use cached, normalized brightness and expose
write failures rather than assuming success. Direct writes may fall back to
logind for an active session. The stock CM0 kernel exposes no backlight class
device. The experimental RM69A10 patch is not approved for deployment; retain
the stock driver.

An opt-in `backlight_backend=wayland_gamma` uses compositor gamma tables for
software display dimming. `backlight_output` selects a `wl_output.name`; an
empty selection is accepted only when exactly one output exists. The connection
uses `WAYLAND_DISPLAY` and `XDG_RUNTIME_DIR`, or an explicitly configured absolute
`backlight_socket`. Multiple outputs never fall back to the first device.
Exclusive-control rejection, output removal and compositor errors make the
control unavailable. Shutdown disconnects the client, restoring the original
gamma table according to the protocol. Cached levels describe accepted software
attenuation, not measured optical output or panel hardware brightness. The
default backend remains `sysfs`; the CM0 profile has not enabled gamma while
physical confirmation is pending. Gamma does not enable output power cycling.

Protocol Probe uses a background Linux receive session. Configuration and TX
completion govern dwell/response timers; generation tokens isolate retunes and
prevent stale cleanup or packets from crossing candidates. Device I/O and lease
release run on the worker, with bounded RX/TX storage. See
[foreground receive sessions](../../docs/engineering/linux-lora-receive-session.md).

Keyboard illumination has a separate optional PWM worker. Profiles configure
`keyboard_pwm_of_node`, `keyboard_pwm_channel`, `keyboard_pwm_period_ns`,
`keyboard_pwm_max_duty_permille` and `keyboard_pwm_export`. The controller is
matched by its device-tree identity under `/sys/firmware/devicetree/base`, so a
changing `pwmchip` number does not select another device. Multiple matches are
unavailable rather than selecting the first. Test/custom systems may override
`keyboard_pwm_root` and `keyboard_pwm_device_tree_root` explicitly.

The CM0 profile declares the software PWM node and a 100-permille full-scale
duty limit, matching the vendor keyboard's on setting. Existing periods and
levels are retained at startup. With export enabled, an uninitialized disabled
channel receives the declared initial period; the app does not claim unrelated
controllers. Requests and readback run in the worker, while UI getters return
cached normalized levels. The shared menu advertises `K` to cycle keyboard
illumination only while the runtime capability is available. Unconfigured
profiles remain unavailable. This implementation uses ordinary sysfs access;
it does not change the kernel, permissions or display brightness.

## Radio protocol integration

Radio configuration and RX-start failures invalidate readiness and block packet
I/O even if a backend retains its old online flag. Initialization failures use
bounded nonblocking retry backoff rather than resetting hardware every UI tick.
Explicit settings changes permit an immediate retry. This recovery policy does
not itself make the underlying driver asynchronous. Application services now
wrap the mesh engine in `LinuxMeshWorker`: configuration, crypto, receive polling
and sends execute on one worker; UI callers enqueue bounded work and read cached
state. Success from a send request means queue acceptance. Physical completion,
delivery or failure updates the protocol-tagged message ledger separately.
Metadata updates preserve queued receive data. Protocol/radio-context changes
cancel old queued work; already-dispatched work reports its original protocol.
Shutdown joins the owner and drains outcomes. Direct power-control and foreground
probe call paths still require latency work.

The shared Linux radio adapter now supports MeshCore encrypted group text and
group application data using enabled channel slots and the configured V1/V2
payload profile. It bounds receive/deduplication state and rejects wrong keys,
corrupt data and UI-only channels. MeshCore identities are persisted independently
from Meshtastic keys. Send ID Local/Broadcast send signed advertisements; verified
incoming advertisements update contacts and valid advertised positions. A signed
advertisement proves key possession, not manual verification of the peer.
Scan Local sends tagged local discovery requests, collects matching responses
for five seconds, and schedules bounded delayed replies to incoming requests.
Its unsigned responses create provisional discoveries and do not establish
trusted peer keys. Concurrent scans/ID announcements return Busy during receive
collection; configuration changes cancel pending discovery work.
Private text and application data use identity-derived shared secrets and require
a peer key learned from a signed announcement. They currently send using flood
fallback. Private text and requested app-data transport ACKs have bounded waiting
and reply state; encrypted PATH ACKs match the expected peer/token, while legacy
short ACKs require a unique pending token. Radio changes cancel stale work.
MeshCore route/path learning, remaining control workflows, identity import/export
and Reticulum remain incomplete; unsupported operations fail rather than sending
the old TML1 frame. The current production GD binary envelope has no explicit
length, so zero-ended application payloads are rejected to avoid silent loss.
Peer interoperability has not been tested.

Reticulum IP calls can wait for verified public-key and telephony-path discovery
when the target identity is known. The outgoing call remains cancellable while
waiting, and its original link ID is retained when the route becomes available.
The wait has a 30-second deadline; invalid signatures, mismatched identities,
ignored peers, transport loss and cancellation do not send a handshake. Missing
identity information still requires discovery before dialing. See
[call path waiting](../../docs/engineering/linux-pending-call-path.md).

## Walkie and device identity

Linux media can select SDL device names through `TRAIL_MATE_AUDIO_INPUT_DEVICE`
and `TRAIL_MATE_AUDIO_OUTPUT_DEVICE`. The common output selection covers music,
notifications and voice playback. A workflow's explicit capture selection takes
precedence over the common input. Empty/unset selections follow the desktop's
default route; an explicit unavailable selection fails to open instead of
silently choosing another device. Run `trailmate-linux-audio-probe --list-devices`
to inspect names exposed by the selected SDL backend without starting capture.

The media-enabled walkie backend uses Codec2 3200, WT v2 packets and GFSK through
the foreground packet-radio port. Hold Space while the walkie page is active,
or hold its touch PTT button. Release, focus loss and page exit stop PTT.
The monitor setting keeps receive active after leaving the page; typing Space
in another application does not start a transmission. Microphone selection uses
`TRAIL_MATE_WALKIE_INPUT_DEVICE`, then `TRAIL_MATE_AUDIO_INPUT_DEVICE`, then the
session default. Audio or radio failures
are displayed from cached worker status; no synthetic levels are generated.
Mesh traffic pauses during the session and resumes with its latest settings.
Peer and physical audio validation remain pending.

Profiles select `runtime_mode=mesh|local|demo`; `TRAIL_MATE_RUNTIME_MODE` overrides
the profile. CM0 ships in mesh mode. Production modes store an independent node
ID in each device's SQLite state root. `TRAIL_MATE_NODE_ID` overrides the process
base/Meshtastic ID using decimal or `0x` notation without replacing that stored
identity. MeshCore uses its own persistent public-key-derived ID. On
upgrading an old installation that used the fixed ID, peers may need rediscovery
or re-pairing. Explicitly pin an old ID only when it is unique on the network.
Settings, history and existing key tables are retained.

## SSTV reception

The media-enabled build uses native microphone PCM at 15 kHz and the portable
`core_sstv` DSP. Receive/stop actions are asynchronous. Capture holds SSTV audio
focus to interrupt music, handles bounded input overruns by reacquiring sync,
and publishes coherent preview copies to the shared page. Silence does not
generate an image. A completed image is saved as a uniquely named BMP under
the SD data root's `sstv` directory; `TRAIL_MATE_SSTV_OUTPUT_DIR` overrides it.
Saving and decoding never run in an LVGL callback.

`TRAIL_MATE_SSTV_INPUT_DEVICE` optionally selects an SDL input device; otherwise
the common input selection or session default is used. For explicit offline PCM input,
`TRAIL_MATE_SSTV_PCM_FILE` reads signed little-endian 16-bit mono samples at
exactly 15000 Hz. This is not a WAV parser. Missing/truncated/incomplete inputs
report errors rather than generating a completed image. The inherited DSP
acquires modes from scan-sync cadence, not VIS. Acquisition can omit initial
lines even when the input contains a header. First-row alignment and full
on-air/multi-mode validation remain pending.

The non-installed `trailmate-linux-sstv-probe INPUT_PCM OUTPUT_DIRECTORY` build
tool runs this exact decoder/persistence path with explicit offline input and a
bounded wait. It is useful for comparing native/target results without opening
an input device. The BW8 test fixture produced identical BMP bytes on the host
and CM0 ARM CPU.

### Bounded native audio probe

Media builds also provide the non-installed `trailmate-linux-audio-probe` tool:

```sh
./trailmate-linux-audio-probe --capture-ms 1000 --rate 15000 --play-silence
```

The explicit capture duration must be 100-5000 ms and the rate 8000-48000 Hz.
Use `--input NAME` to select an SDL capture device instead of the desktop
default. The probe reports sample counts, nonzero sample counts, peak amplitude,
dropped samples and whether capture stopped cleanly. It never saves or
transmits a recording. `--play-silence` additionally opens playback, queues
100 ms of digital silence, drains the queue and closes the device. Transport
success does not prove physical microphone input or audible speaker output.
All-zero capture may require checking mute, codec routing, input wiring and
the connected microphone rather than assuming that device enumeration proves
readiness. A dummy-audio CTest exercises the bounded lifecycle on the host.

### Native Reticulum status

Selecting Reticulum now initializes a separate persistent X25519/Ed25519
identity on the mesh worker. Public identity/address information is read from
a cached snapshot; damaged identity storage is reported without generating a
replacement. Incoming announcements now pass the shared signature/destination
verifier and existing path manager, with copied RX counters in diagnostics.
Paths remain transient and do not grant contact trust. Directory path requests
can be queued through the mesh port with TX permission, cooldown and expiry
checks. Verified person announcements now enter the shared identity registry
and thread-safe native peer directory; service announcements do not become
contacts. Local trust/ignore/favorite/alias policy is preserved. The shared link
owner now handles handshake/control packets. Signed plain text within a link's
packet-size limit reaches the existing chat service and SQLite store; its
packet proof is sent only after durable application acceptance. Bad signatures,
ignored sources and unsupported payloads are not acknowledged as text. Plain
peer-text sending now discovers paths/keys, establishes links and reports Sent
only after complete radio submission, then Delivered only after a matching
signed packet proof or validated resource completion proof. Larger peer-text
envelopes use encrypted unsplit resources, optionally compressed with bzip2;
incoming confirmation still waits for durable storage. Split resources,
metadata, group/opportunistic messages, custom/application fields,
propagation and dependent network workflows remain unavailable. See
[Linux Reticulum integration](../../docs/engineering/linux-reticulum-integration.md).

### Native Geocaching status

Local drafts and authenticated recovered offline copies feed the shared Map
overlay through bounded asynchronous windows. Saved position/name changes,
archival and local deletion refresh a stationary Map; archived drafts and
unknown coordinates are excluded. Local drafts remain usable when offline
storage is missing, and the browse page retains its storage error. Directory
discovery, live download and signed publication still require the native
Reticulum/LXMF transport and remaining business composition. See
[Linux geocaching integration](../../docs/engineering/linux-geocaching-integration.md).

### Native Agenda status

The Linux Agenda store, clock and background source are available for composition. They
reuse the core record encoding, stable IDs, recurrence and reminder rules, and
the clock follows product/system timezone settings with suspend-aware monotonic
time. The worker publishes sealed read projections and drains accepted commands
on shutdown. The entry point creates the service and supplies Calendar to the
shared catalog, page/Map flow, dashboard and reminder popup. Popups defer while
audio focus is busy. A separate native worker persists saved waypoints in the
same configured database and supplies the shared location picker. Saving a
location as a waypoint waits for a durable commit; selection uses bounded
pagination over a sealed read projection without SQLite work on the UI thread. See
[Linux Agenda integration](../../docs/engineering/linux-agenda-integration.md).

### VMP audio backend status

Media builds include a worker-owned `NativeVoiceAudio` adapter matching the
production Codec2-1300 voice profile: 8 kHz mono, at most five seconds and 875
encoded bytes. It supports early stop, cancellation, bounded playback, volume
and exclusive audio focus. A SQLite attachment port now persists the shared
eight-entry inbox, including stable local IDs, read/delivery state and safe
restoration of interrupted sends. Application services now own the voice worker
and bind the shared chat voice interface on compatible media/radio compositions.
A passing codec/PCM test does not establish a
working physical microphone or a complete voice-message send.

The direct-RF port now supports VMP profile 1 through a foreground radio lease
and isolates VMP control envelopes before generic mesh parsing. Backend-specific
capability checks keep SX126x constraints separate from the LR2021 media PHY.
Checked release reports a failed normal-radio restoration rather than silently
claiming success. A worker-owned session executor now implements broadcast and
verified-contact private handshakes, authenticated media/FEC recovery, bounded
waits and cancellation. Private ACCEPT replies require a durable one-time
reservation, preserving nonce-use history independently of retained messages.
The reservation journal must not be erased or rolled back while retaining the
same contact-key schedule. The bounded application mailbox and UI binding now
connect public record/send, incoming attachments, playback and read state.
Storage and RF work remain on the voice/mesh workers; protocol switches cancel
old-context jobs. An injected mesh radio requires its matching foreground port.
The verified-contact provider now derives VMP-only keys from the existing MT/MC
identities and manually verified public directory pins. Public keys travel from
the mesh worker's cached snapshot into that directory; discovery never grants
manual verification. Trust changes cancel old jobs, and derivation racing a
revocation is discarded. MT and MC number/code verification are connected to
the shared UI. MC uses authenticated V1/V2 direct controls and its existing
identity shared secret; user comparison acceptance grants the directory pin.
The shared fixed-control padding decoder preserves zero bytes at the end of
the nonce. Unverified private requests continue to fail closed. The real CM0 capture probe still returns
only zero samples; physical microphone/speaker and peer RF validation remain
unproven.

The RX-only `trailmate-linux-radio-probe` build tool accepts `--vmp-channel 0..39`
to configure this media profile temporarily, then restores LoRa before releasing
the radio. Supply the SPI/GPIO/TCXO arguments from the target profile. This probe
never transmits; configuration success does not establish peer interoperability.
## External resource storage

The Linux composition reads installed language, font and IME resources from
`<resolved data root>/trailmate/packs`. `TRAIL_MATE_RESOURCE_PACK_ROOT` overrides
this complete resource directory. The local package catalog defaults to
`<resolved data root>/pack-catalog`, with `TRAIL_MATE_PACK_ROOT` as its override.
Catalog entries and installed resources are distinct. Local directory packages
install real resources and installed records through atomic snapshots. Shared
resources are retained while another installed package owns them; conflicting
contents reject the operation. Minimum firmware versions and supported memory
profiles are evaluated for listing and rechecked against the staged manifest
before publication. The runtime build version follows the same CI tag/Changelog
source as firmware builds; package versions default to its Debian form. Explicit
package-version overrides remain available. Remote catalog/archive delivery
remains incomplete. See
[resource storage](../../docs/engineering/linux-resource-pack-storage.md).

## Phone Bluetooth scope

Phone Bluetooth is intentionally disabled for the current Linux release,
including T-Display-CM0 and Cardputer Zero. Legacy configuration and composition
flags cannot start the phone service. The retained transport and session code
is deferred development material, not a supported application feature. Pairing,
phone administration and Bluetooth interoperability are outside this delivery.
The application does not require BlueZ or GIO; no further Bluetooth controller
or boot configuration changes are part of this work.
Application restart requests now release run-owned services before re-executing
with the original arguments; window close and Alt+F4 take priority. Clock writes
use the operating-system interface and report authorization failures.
See the [runtime integration record](../../docs/engineering/linux-phone-runtime.md)
for the remaining implementation and verification scope.
