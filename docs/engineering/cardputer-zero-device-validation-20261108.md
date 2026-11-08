# Cardputer Zero device validation — 2026-11-08

For the current communication, UI, sleep and storage acceptance, see the later
[paired three-protocol qualification](linux-paired-mesh-validation-20261108.md).
It supersedes the historical outstanding private-chat items recorded below.

## Accepted GPS scope

At 17:06 CST the user explicitly accepted satellite reception as sufficient for
GPS acceptance. This criterion is met: current NMEA traffic is valid, GSV records
contain visible satellites and signal values, and the real GPS widget displays
the satellite plot and separate used/visible counts. A successful position fix
is not required for this deployment's GPS acceptance and no longer blocks it.
The receiver still has no valid position; historical fix investigations below
remain evidence, not an outstanding requirement. Real private-chat peer receipt
or a delivery ACK remains unconfirmed and was not waived by this scope update.

## Source and installed artifact

The later paired-device session superseded the package snapshot below: Zero
now runs `1.0.1+cardputer.20261108.11`, including the SX1262 receive-IRQ restore
fix. Physical NodeInfo discovery and private text with delivery ACKs passed in
both directions against T-Display-CM0 on CN/478.875 MHz. See the
[paired-device validation record](linux-paired-meshtastic-validation-20261108.md)
for final package hashes, driver fixes and device evidence. Earlier `.10`
map, framebuffer, UI and GPS results below remain historical qualification.

The working checkout was fast-forwarded from `05d75d66` to
`4732374d0457429db16bb8d70e02dcfdf4a9ca89` before building the adaptation.
Existing local changes were preserved. The deployed package includes the
uncommitted Cardputer menu, widget, Cap power/presence, magnetic heading and
framebuffer presentation changes in this checkout.

- Debian package: `trailmate-cardputer-zero`
- Installed version: `1.0.1+cardputer.20261108.10`
- Architecture: `arm64`
- Package SHA-256: `c1e8dc5de19df385ca6e1fad0268b8db88b8a26a5ab55688d3e35dc61315ed53`
- Device OS: Debian 13, kernel `6.18.39+rpt-rpi-v8`
- Display: `/dev/fb0`, 320 × 170, RGB565, 640-byte stride
- Keyboard: `/dev/input/cardputer-zero-internal`

This OS runs APPLaunch directly on the framebuffer. Its explicit system
environment selects `TRAIL_MATE_DISPLAY_BACKEND=fbdev`, `/dev/fb0` and the
keyboard alias. The package's Wayland default was retained. Installation,
runtime library resolution, normal APPLaunch launch, keyboard focus movement
and physical backlight setting were checked on the device as user `pi`.

## Menu and widgets

The menu uses the shared Pager grid, adapted to four columns and two visible
rows. All 13 current application routes remain registered. V opens/closes the
widget feed; Up/Down or W/S select a page, and Left/Right or A/D plus Enter
operate supported widget controls.

Seven widget pages were captured on the actual framebuffer: Mesh, GPS, Recent,
Compass, Agenda, Music and Controls. The Linux magnetic sensor runtime reads
the BMM150 IIO device in its existing background sensor worker. The compass
reports magnetic heading, explicitly labelled uncalibrated and requiring the
device to be held level. True north, tilt compensation and calibration accuracy
were not established by this test.

## Framebuffer flashing

The old presenter cleared the live framebuffer and repainted it byte by byte
on every presentation request, including unchanged frames. The replacement
converts into private native-format storage, publishes with a contiguous copy,
and skips conversion and framebuffer writes when the Canvas pixels are
unchanged.

A regression fixture verifies 1,000 unchanged presentations produce one
commit. A separate reader samples alternating nonzero frames to detect the
previous clearing interval. The device was also sampled without pausing its
UI process:

| Scene | Duration | Samples | Distinct frames | Maximum zero-byte fraction | Process CPU, one core |
| --- | ---: | ---: | ---: | ---: | ---: |
| Idle menu | 6.019 s | 283 | 1 | 0 | 33.89% |
| Compass widget | 6.002 s | 283 | 5 | 0 | 34.65% |

The earlier device run consumed approximately 62% of one core. These samples
support elimination of the observed live-clear flashing; they do not prove
tear-free scanout or optimal total background CPU usage. The main UI thread
used approximately 4.5% of one core in a separate short thread sample; remaining
worker activity is a further performance investigation.

## Cap power, presence and GPS

The Cap was reported physically attached. Its external 5 V supply was initially
off. Enabling the OS-owned `ext_5v_out` endpoint changed the SX1262 from
all-zero readback to a responding radio, and enabled valid GPS NMEA traffic.
The shell now owns this supply for the service lifetime and restores the
previous value after workers stop. A scoped udev rule grants the GPIO group
write access to this attribute.

Presence uses a real radio response or GPS bytes. An eight-second startup
grace period precedes one brief notice when neither responds. Permission and
resource conflicts have a distinct notice; disabled services do not imply a
missing Cap. The missing-device scenarios were covered by the focused test
fixture. A physical removal test was not performed; the module must not be
hot-swapped.

GPS parsing was verified, but the receiver had no position fix and reported
`ANTENNA OPEN`. Navigation accuracy and satellite acquisition are consequently
unverified.

## Meshtastic CN radio exchange

The peer was reported to use CN, default LongFast and the default PSK. A
temporary diagnostic executable linked to the same application service and
radio libraries exercised the existing business APIs. MQTT was disabled.
The saved database was backed up on the device before changing configuration.

Actual derived settings were CN, 478.875 MHz, slot 35, bandwidth 250 kHz, SF11,
CR 4/5, sync word `0x2B`, preamble 16, primary channel hash `0x08` and 5 dBm TX.
Local identity was `!4BE5DDEF`, `CZ / Cardputer Zero`.

The diagnostic sent a NodeInfo broadcast, a NodeInfo broadcast with
`want_response`, and a marked broadcast text. Driver counters advanced to
TX 3 with no reported TX timeout. No received frames, peer NodeInfo, ACK or
private-chat exchange were observed during approximately 150 seconds. A
private message was conditional on discovering a peer; none was sent because
no peer was received.

The diagnostic temporarily stopped the UI to obtain exclusive radio ownership.
This left the last framebuffer image visible and was mistaken for a freeze.
APPLaunch and Trail Mate were restored, and keyboard movement was checked.
After restoring the UI, its independent LoRa master switch was found disabled
and re-enabled at 11:45:53 CST. A subsequent peer-triggered NodeInfo broadcast
was monitored with the normal UI running; RX remained zero through the
11:48:53 CST sample. The app was left running with LoRa enabled on the above
CN settings.

The official [Cap LoRa-1262 documentation](https://docs.m5stack.com/en/cap/Cap_LoRa-1262)
specifies an 868–923 MHz module/antenna range and a PI4IO-controlled antenna
switch. CN's derived 478.875 MHz falls outside that advertised module band.
This is a possible hardware compatibility cause, not a proven diagnosis.
The documented I2C address `0x43` did not respond on the device's exposed I2C
buses, including a short reversible check with the Grove 5 V supply enabled.
No undocumented GPIO routing changes were made. The exact fitted Cap revision,
antenna band and peer's actual tuned frequency still need reconciliation.

Chip TX completion establishes the local command/driver path only. This run
does **not** establish successful over-the-air delivery, NodeInfo exchange,
PKI interoperability or private-chat delivery.

## Follow-up: received NodeInfo and keyboard chat

The normal application subsequently received three NodeInfo packets at
11:52:26, 11:52:53 and 11:52:54 CST. All decoded to `!A1B3B57C`,
`B57C / TM-B57C`, with RSSI between -88 and -95 dBm and SNR between 5.8 and
7.5 dB. The peer's broadcast text `hi` was also decoded, stored and displayed
in Chat. This establishes real CN reception and NodeInfo processing; the
earlier advertised-band concern is not a demonstrated explanation for the
initial lack of packets.

Using the ordinary Contacts → Nearby → Chat route, the app transmitted
`cardputer zero private test please reply` to that peer using PKI. Its status
was Sent, with no ACK observed. After installing `.4`, the real keyboard path
typed `new keyboard send test`, moved directly to Send with one Tab press,
and sent it with Enter. Exactly one corresponding durable message record
was created, and the radio logged a PKI direct transmission to the correct
peer. Peer receipt/private reply remains unconfirmed.

At 12:17:04 CST, `.5` also broadcast the existing `CZ / Cardputer Zero`
NodeInfo again through the normal Settings save/application command path,
without changing the node name or regenerating its identity. This gives the
currently listening peer another opportunity to obtain the local public key.

The Cardputer Compose action bar was reduced from 44 to 26 pixels, with
20-pixel-high buttons. Conversation actions occupy 24 pixels. A visible
`Tab to Send, then Enter` hint describes keyboard submission, while Enter
inside the text field remains a newline. A focused regression fixture also
exposed and corrected two send callbacks for a single physical Enter key
press; the Cardputer now uses LVGL's ordinary release/click event once.

The remaining hot background thread was identified as `LinuxAgendaSource`.
Its 100 ms idle sampling repeatedly read timezone settings from SQLite.
The idle wait is now one second, with commands and presentation changes
retaining immediate wakeups. The regression first failed on the old idle
sampling rate and passed after the change; reminders were also tested without
an explicit refresh request. On-device `.4` Chat sampling measured 15.33% of
one CPU core over 6.003 seconds, with 282 samples and one distinct frame.

The OS exit gesture was tested separately. `.4` did not exit on a long Esc
hold because the framebuffer input adapter did not track that hold. `.5`
tracks the hold, ignores repeats for timing, and cancels it after a lost-input
notification. The on-device hold test exited successfully, restored EXT5V
to zero and left APPLaunch active. Normal APPLaunch re-entry was verified.

## Make TMAP: global online workflow

Version `.10` bundles the unchanged Center producer built from clean
`trail-mate-center` revision `efa12462a526aabad7212cbadfa537ace0b7dbd6`.
The shared wizard follows its area-first workflow: administrative search,
map content/detail, suggested save metadata, review, and creation/result.
Coordinates and a local PBF are optional alternatives. Photon supplies global
administrative extents; namesakes include their country and parent context.
Only real extents are accepted. Creation uses the enclosing rectangle rather
than clipping to the administrative polygon.

The physical keyboard successfully searched `vatican`, selected Vatican City
(`R36989`, VA), and advanced through the entire review. The selection was
approximately 1.0 by 0.8 km, with Balanced choosing zoom 13–14 and four tiles.
The review estimated 0.5 MiB of raw pixels and 257.5 MiB working space, against
approximately 20.5 GiB free. Compact controls use the existing amber theme;
Tab and Enter reach every action. The completion action opens the generated
area in the existing Map renderer, and Back returns to the result.

Online source acquisition is streamed XML followed by `osmium` conversion,
then Center build and verification. Jobs are capped at 2500 square kilometres,
64 MiB of downloaded source and a default 128 MiB output budget. These are
small-device limits, not a claim that arbitrary countries can be generated.

Actual network validation exposed direct-network failures. The original
`overpass-api.de` endpoint timed out and also returned HTTP 502. A diagnostic
request to Private.coffee successfully downloaded 4,898,944 bytes in 24.287 s,
but the subsequent `.8` application job timed out after its 150 s limit.
The UI remained responsive, showed the failure and Retry/edit, and removed its
job workspace. App RSS peaked at 38,228 KiB in that run; the producer was never
started, so this is not a successful production-memory measurement. Repeating
the exact request with IPv4 and POST also timed out; a second provider returned
HTTP 504. These failed direct requests did not pass online acceptance; the
subsequent explicit-proxy run below did.

The user's existing desktop Clash was subsequently reused through a loopback-only
SSH reverse forward: device `127.0.0.1:17897` to the tool host's
`127.0.0.1:7897`. An explicit-proxy request returned Photon HTTP 200 in 2.156 s
and the single-output Overpass response, 3,491,251 bytes, HTTP 200 in 84.835 s.
The separate user-specified LAN host `192.168.50.210` answers ping, but its
7897 TCP port still timed out in the latest check. No public proxy listener or
Clash subscription/configuration copy was created. The SSH bridge requires the
tool host and its Clash to remain running; it is not a standalone proxy installed
on Zero. Trail Mate's user environment was configured for this working bridge,
with the previous environment backed up on the device. The initial in-memory
password bridge was replaced by a hidden, reconnecting background process using
a dedicated restricted SSH key. That key cannot run a shell, allocate a PTY,
forward an agent or run a user RC; its reverse listener is restricted to
`127.0.0.1:17897`. Its private half remains in the local SSH directory, and no
login password is stored. The restart helper and logs live beneath
`%LOCALAPPDATA%\TrailMate\CardputerZeroProxy`; no automatic logon task is installed.

The ordinary `.9` keyboard workflow then completed a real online job through
this Clash path. The monitor recorded 115.281 s through completion, a sampled
app peak RSS of 38,564 KiB and producer peak RSS of 145,052 KiB. The installed
`maps/tmap/osm/area-R36989-osm.tmap` is 10,342,400 bytes. No local PBF was selected:
the application downloaded its own 3,491,251-byte XML source and converted it.
The private job directory was removed, leaving its namespace owner marker.
View map displayed actual Vatican roads and labels at zoom 14 on the framebuffer;
Esc returned to the successful result with View map focused.

Version `.10` additionally replaces the zero-byte download caption with
`Waiting for map server (N s)` until bytes arrive, so server processing does not
look like a stalled transfer. Its source/HTTP/Center/SDK integration, keyboard
flow and compact/P4 layout regressions all passed again; the preceding full
32-test suite and separate agenda regression passed on `.9`.

The final `.10` package was installed and launched normally through APPLaunch.
A second actual-device online job, revision 2, completed in 125.114 monitored
seconds. Its XML source was 3,540,548 bytes and its final
`maps/tmap/osm/vaticanr2-osm.tmap` is 10,350,592 bytes. The app's sampled peak RSS
was 37,204 KiB; the producer's was 143,672 KiB. The actual framebuffer showed
`Waiting for map server (5 s)` before transfer. The private workspace was again
removed after successful production, verification and installation.
View map and Esc were repeated on this final build, and both displayed the
generated map and returned to the successful result. The device was left in
the generated map view.

The retained `built-source-manifest.json` records hashes for 52 modified compiled
inputs and verifies that all match the deployed build mirror. The final local
package directory retains `.10` only; obsolete packages, ad hoc regression
binaries/objects, the producer source tar and unused offline-catalog research
dependencies were removed after validation.

Real keyboard inspection also exposed an open-dropdown navigation conflict:
the shared Pager router consumed Up/Down before the popup could select an option.
The new regression failed on the previous code (exit 134), then passed after
open dropdowns were excluded from ordinary focus navigation. Version `.9`
includes that fix and popup theme/font adjustments.

The installed package already contains the concurrent node-discovery changes
subsequently committed as `8bc50ffc22aab1ea2c88472c9dcc3050e61590d4`.
Its 16 committed files were compared byte-for-byte with the build source mirror;
all matched. The package additionally contains this task's uncommitted changes.

## Automated checks

The final selected native suite passed **32/32** tests, including the 13 application
help/navigation cases, calculator, menu profile, Cap presence/power, widget
feed, keyboard chat submission/compact layout, short/long Esc handling,
magnetic heading, IIO, framebuffer presentation, Meshtastic ACK tracking,
TMAP keyboard flow, online source acquisition and packaging checks. The
separate full Linux agenda-source regression also passed. After the final
single-output Overpass query adjustment, the online integration test was rerun
successfully against the real Center producer and SDK tile/search reader.
The five-page keyboard test and both compact/P4 creator layout regressions
also passed. These localhost fixtures establish program behaviour; the separate
actual-device network and map-display acceptance is described above.
The final cross-package build exited successfully. `git diff --check`, the
platform UI boundary checker and root build artifact checker passed.

GitNexus refresh was attempted twice and failed on parser worker timeouts.
Available impact results were LOW where symbols were indexed, and UNKNOWN
for new/unindexed symbols. Its change report is therefore partial, not proof
of complete call-graph coverage. No commit was made.

## Remaining physical acceptance, 14:31 CST

A fresh completion audit confirmed `.10` running as PID 124101, Cap power enabled,
no creator job directories, and the loopback proxy listener still present.
GPS sentences continued to parse, but GSV reported zero visible satellites,
GLL remained invalid, and GPTXT still reported `ANTENNA OPEN`. This is not a
successful position-fix test. Physical antenna/sky-view confirmation was requested.

The radio remained in CN/LongFast receive monitoring. Its most recent actual
received peer records were the earlier NodeInfo and `hi`; no later private reply
or Routing ACK was found, and outgoing private chat records remained Sent.
Fresh peer availability and an incoming private message were requested before
another over-the-air private exchange. The UI was left running throughout.

The persistent bridge's dedicated key was also tested independently: shell
execution, another reverse-listener port and a direct tunnel to another local
target were all denied by the SSH server. The intended existing loopback bridge
remained available. LAN proxy `192.168.50.210:7897` still timed out on a new probe.

## Outdoor GPS follow-up

After the user moved the device outdoors, the receiver's output changed:
UTC time/date became valid, GP GSV reported four visible satellites with signal
values, and BD GSV reported one. Seven samples from 14:38:17 through 14:40:17 CST
still had GGA quality 0 / satellites used 00, RMC status V, and no position.
The read-only observation used the application's existing log rather than
opening or interrupting its serial device.

The current [official Cap LoRa-1262 documentation](https://docs.m5stack.com/en/cap/Cap_LoRa-1262)
specifies an ATGM336H-6N/AT6668 receiver with a built-in ceramic GPS antenna.
The earlier request to confirm an external GPS antenna was premature.
`ANTENNA OPEN` remains a receiver diagnostic, but it alone does not establish
that the built-in antenna is disconnected or that hardware is defective.
Acceptance requires a genuine receiver fix and valid coordinates.
The Linux NMEA path opens the serial device read-only and does not send
constellation-configuration commands. The exact outdoor sky view was requested
before attributing the remaining lack of a fix to software or hardware.

The user clarified that the current position is a balcony or window. At
14:42:53 CST the latest GP GSV reported three visible satellites, with GGA
quality 0 and satellites used 00. This placement does not rule out obstruction
or reflected signals; [GPS.gov](https://www.gps.gov/gps-accuracy) identifies
building blockage and reflected signals as common positioning problems.
An unobstructed outdoor comparison, retaining power and allowing a short
acquisition window, is the next physical test. No receiver reset, forced fix,
constellation rewrite or application restart was performed.

On the explicitly resumed run, the app was still PID 124101 at 16:52 CST.
The retained rotating logs covered 16:43:46–16:52:33 and contained 527 GGA and
527 RMC records, with no valid fix in either set. Visible counts were GP 6,
GL 1, GA 1 and BD 3, but several entries had no signal value. At 16:54:21,
the GP entries at azimuths 020 and 038 had empty signal values, while reported
signals were concentrated toward azimuths 143–227. This is consistent with
restricted sky view, not proof of a hardware defect or a complete diagnosis.

The official product photos locate the built-in antenna beneath the side marked
`GNSS / ANTENNA Inside` next to the blue location icon. An unobstructed outdoor
comparison with this face toward the sky was requested. No application or
receiver changes were made while awaiting that physical comparison. The latest
peer reception and private-delivery counts were unchanged, and the LAN proxy
port still timed out; the existing SSH/Clash bridge remained running.

The user then confirmed that only balcony placement was available and adjusted
the GNSS face toward the open side, keeping the device still. A new bounded
observation ran from 16:57:04 through 17:00:04 CST and exited normally after ten
samples. Every sample reported GP 7, GL 1, GA 1 and BD 3 visible entries, with
seven entries carrying a signal value and the strongest ranging from 39 to 35.
All ten still had GGA quality 0, satellites used 00 and no valid combined
GGA/RMC fix. The raw result is retained on the device as a mode-0600 JSONL file.

The live GPS widget was opened through ordinary keyboard navigation without
restarting the app or receiver. Its satellite plot displayed received satellites
and `Used 0/12`, `NO FIX`, matching the receiver data. This establishes working
serial reception and satellite-status presentation, not successful navigation.
The device was left on this widget so visible satellites and satellites used
are directly distinguishable. Further unrestricted-sky tests are unavailable
under the user's current physical constraint; the cause of no fix is not proven.
