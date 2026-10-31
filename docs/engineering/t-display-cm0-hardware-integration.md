# T-Display-CM0 hardware integration

This is an incomplete integration ledger, not a release-completion claim.
The required end state is every fitted CM0 peripheral connected through Linux
platform ports to the applicable Trail Mate features, with real-device evidence.
The P4 touch UX and the standard armhf Debian package remain required.

Use the [current acceptance audit](cm0-adaptation-acceptance.md) for the
requirement/evidence map and open compatibility review. Sections below retain
chronological findings; later verified implementations supersede older gaps.

## Probe transmit recovery, 30 October 2026

The Linux LoRa UI port now checks RX restart after transmitting. If restarting
reception fails, the operation returns failure and invalidates the receive
context. RSSI, packet polling and subsequent transmissions remain unavailable
until explicit reconfiguration succeeds. This also holds for a backend whose
online flag remains optimistically true. The prior behavior was reproduced by
fault injection: a failed RX restart incorrectly returned transmit success.

The normal dependency build rebuilt the production port, linked the main
application and rebuilt both Probe page harnesses. Four focused tests passed
(receive state, asynchronous session, Probe session page and IQ page), and the
compact IQ page passed separately. The P4 equivalent fix passed its exact-source
test, the complete native FM/TV suite (17/17) and the TFT LR2021 firmware build.
No physical TX/RX acceptance was performed. CM0 and its LR2021 remain reserved
for the user; this verification used local software and explicit test boundaries.

## Current display evidence, 23 October 2026

The stock Labwc session advertises one `wl_output`,
`zwlr_gamma_control_manager_v1` version 1 and
`zwlr_output_power_manager_v1` version 1. Its sysfs backlight class is empty.
The standard installed package still selects the sysfs brightness adapter and
`screen_power=none`. It now contains an optional Wayland gamma
adapter to the asynchronous brightness service, selected with
`backlight_backend=wayland_gamma`, `backlight_output` and `backlight_socket`.
It has not been enabled in the CM0 profile. Installation of the current deb did
not acquire gamma control or change output power.

A bounded standalone Wayland probe acquired gamma control, submitted an
85-percent linear RGB ramp, held it for four seconds, destroyed the control
and completed a compositor round trip. No protocol failure was received.
The process exited successfully under a ten-second timeout. This test changed
neither the kernel driver nor boot configuration and did not power off the
output. The user reported no noticeable dimming and a normal picture. Optical
dimming effectiveness is therefore unconfirmed; protocol acceptance alone does
not establish optical behavior. The stock driver and `screen_power=none` remain
selected, with no further display experiments planned.

The [upstream gamma-control protocol](https://github.com/swaywm/wlr-protocols/blob/master/unstable/wlr-gamma-control-unstable-v1.xml)
specifies restoration of the original gamma table when the control is destroyed.
It offers no original-table readback. Gamma attenuation is software display
dimming, not evidence that panel DCS brightness or a hardware backlight works.
The source adapter retains explicit backend/output selection, refuses ambiguous
outputs, handles exclusive-control failure, and releases control during shutdown
or transport failure. Initial acquisition does not write a ramp. Reconnection
reapplies only an application-requested level. The compositor socket is
close-on-exec; failed connections retry at a bounded rate on the worker.
Existing sysfs devices retain their default backend. Isolated compositor
fixtures validate actual SCM_RIGHTS LUT transfer and channel layout, named
selection, ownership rejection, submission rejection and disconnect release.
They also reproduce output addition during active control: the initial adapter
incorrectly retained control after a second output appeared. The corrected
adapter releases ownership and lets the worker rediscover/revalidate selection.
Output removal follows the same path. Ten isolated compositor/worker scenarios
passed both on the host and as armhf executables on the actual CM0. The CM0 run
used temporary sockets, did not connect to Labwc, and passed the current board
profile parser fixture. The ARM build exited with code zero using the target
Raspbian sysroot and C++ ABI overlay. The original sysfs brightness and
device-options host fixtures also pass. Do not
enable automatic output power cycling based on this gamma-only test.

The user confirmed on 21 October that this device has neither the NRF9151/GNSS
expansion nor the QMI8658 IMU expansion installed. Their earlier failed probes
are historical observations of an image configured for optional hardware, not
outstanding faults in fitted peripherals. The baseline profile now disables
those assumptions and leaves the expansion power GPIO unclaimed. Optional
generic backends and their tests remain available for devices that fit them.

The current installed development deb is `trailmate-cm0_1.0.0_armhf.deb`, SHA-256
`90c916e5c242b8c0950227c72ebfc06b14be7105787a690a6a10282404130a39`.
It passed lintian, installation, `dpkg -V`, original-logo comparison and an
isolated startup/timed-exit check. The installed executable's SHA-256 is
`0cbf9c2c81014067bf4ac51031c1c88071da3f8dd0f62f2140fbbd6a72c6c41f`.
The preceding `8ea6b421...` package is retained as
`trailmate-cm0-before-directories.deb` for rollback. This release includes the MT/MC and
VMP MQTT integration and native CCM correction. Four ARM groups passed on CM0:
voice inbox, PKI vectors, MT/MC application-service pairs, and recorded VMP
broadcast/private transfer through separate worker processes. Independent
Mosquitto 2.0.18 interoperability passed on the host. The complete native run
passed 216/217; its sole TMS lifecycle fixture failure was a dependency on real
audio admission in a hardware-independent settings test. After explicitly
isolating its realtime hooks, that one test passed separately. No production
call gate was relaxed. The rebuilt compact suite passed 19/19. Both Cardputer
Zero runtime targets and the ARM application/audio/hardware tools built.
The full-worktree GitNexus audit reported CRITICAL (209 files, 801 symbols,
27 flows), including accumulated changes; no commit was made. ESP stack hygiene
and Reticulum scope guards passed.

The subsequent NodeInfo alignment also passed five native targeted fixtures,
including instrumented V1/V2 group/private codecs and identity-bound responses.
Actual application pairs query and persist distinct short/long names through
MQTT, with no RF-measurement flags or key-trust changes. Independent Mosquitto
interoperability passed again with that assertion enabled. Four ARM groups
passed on CM0 (NodeInfo, group, direct and application MQTT). Both compact
targets rebuilt and the compact suite passed 19/19 again. The current package
passed lintian, installation and installed-app verification. The final cumulative
GitNexus audit reports 209 files, 806 symbols and 27 flows, still CRITICAL;
this is whole-worktree evidence, not a low-risk claim for accumulated changes.
No commit or screenshots were made. See [MeshCore NodeInfo](linux-meshcore-nodeinfo.md).

The subsequent identity management integration passed 7/7 targeted native
checks, followed by a complete 222/222 native run and 19/19 compact tests. Formal
backup/restore/format operations now bind to frontend quiescence, atomic storage
and application restart. Both seed and expanded keys, busy/stale rejection,
rollback and lost-response re-proof are covered. Five ARM groups and independent
TCP/exec/signature checks passed on CM0. The exact installed formal executable
also passed an isolated seed restore, actual exec and fresh author proof.
Only generated temporary identities were changed; the user's current identity
was not exported or replaced. The current package passed lintian, installation,
file/logo integrity and startup/exit checks. The cumulative GitNexus audit is
now 210 files, 823 symbols and 27 flows, still CRITICAL. See
[identity backup and restore](linux-hostlink-identity-backup.md).

The subsequent wireless identity release implements formal operations 7-9 for
both existing radio backends with a separate coordinator, shared frontend
quiescence/restart, ownership leases and atomic key/generation compare-and-swap.
The concentrated gate passed 12/12, the complete native gate 234/234 and compact
gate 19/19. Actual CM0 fixtures passed snapshot, six replacement scenarios, four
atomic local-store scenarios, independent TCP/exec/signature checks for both
radio kinds and author restore regressions. The exact installed application
also passed both radio restore paths using isolated dummy-SDL profiles and
temporary identities. Lintian, installation, file/logo integrity and startup/
timed exit passed. No user identity, display driver or boot image was changed.
The latest whole-worktree GitNexus audit is CRITICAL: 211 files, 841 symbols,
27 flows; no commit was made. See [wireless identity storage](linux-radio-identity-storage.md).

The subsequent track-management release adds formal catalog operation 80 and
snapshot operations 81/86-88 through native worker-owned coordinators. The TSN1
codec is shared with ESP through compatibility wrappers. Full native regression
passed 246/246 and compact regression 19/19; the final post-lock admission guard
passed the affected 21/21 gate and native/compact/ARM incremental builds. A
same-size rewrite regression passed 100 times. On CM0, nine lifecycle/failure
scenarios and an independent four-format TCP/hash/reconnect/release check passed.
The exact installed executable passed that wire chain using temporary roots and
dummy SDL. The standard deb passed lintian on a retry of the unchanged package
after a host Perl failure, installation, file/logo integrity and startup/exit.
No screenshot, reboot or user recording/identity mutation was used. The latest
whole-worktree GitNexus audit reports CRITICAL (212 files, 846 symbols, 27 flows).
See [Linux track management](linux-hostlink-track-management.md).

The subsequent local-signing release adds formal preparation/signing operations
82/96/99. Candidate checks, attachment verification, revision reservations,
durable signatures, recovery and signed heads reuse their shared core contracts.
The native attachment adapter is shared with the existing signed-package install
input. Full native regression passed 250/250, compact regression 19/19, and ARM
application/audio/hardware tools and signing/input fixtures built. On CM0,
preparation/recovery, injected reservation-write failure and signed-package input
regressions passed. Independent TCP/OpenSSL checks passed for V1, V2 assets and
V2 without assets, then passed again against the exact installed executable in
temporary roots with dummy SDL. Lintian, installation, original logo, file
integrity and startup/exit passed. The previous `ac736011...` package is retained
for rollback. No user identity/recording mutation, screenshot, reboot or display
driver change was used. GitNexus reports accumulated CRITICAL scope: 212 files,
847 symbols, 27 flows; no commit was made. Publication/history contracts remain
open. See [local track signing](linux-hostlink-track-signing.md).

The subsequent directory/application-transport release adds formal operations
89/90 through a database-scoped, session-owned metadata seam and native mesh
worker. Standard LXMF custom data shares the verified encrypted link/resource
machinery, with durable/control acceptance before proof and separate application
outcomes from chat. Directory announcements, random-request capabilities and
incarnation changes use the shared protocol validators. The focused gate passed
26/26, complete native regression 263/263 and compact regression 19/19. ARM
application/audio/hardware tools and directory/transport fixtures built. CM0
passed directory state and announcement fixtures, nine custom delivery cases,
real TCP routes and independent HostLink session checks. The exact installed
executable passed those session checks in isolated mesh mode. Lintian,
installation, original icon, `dpkg -V` and startup/timed exit passed. The previous
`8ea6b421...` package is retained for rollback. No current identity/recording
mutation, screenshot, reboot or display-driver change was used. GitNexus reports
the cumulative CRITICAL scope of 213 files, 849 symbols and 27 flows; no commit
was made. Publication/history remain open. See
[directory discovery](linux-hostlink-track-directories.md).

Its four focused LoRa port/session/fallback/page
fixtures previously passed natively and on CM0, with ASan/UBSan on the native worker.
The latest MeshCore forwarding integration passed eight native and eight
assertion-enabled ARM fixtures on CM0; its wire planner also passed ASan/UBSan.
The `1595c016...` package is retained for rollback. See
[MeshCore client forwarding](linux-meshcore-repeater.md).
Both compact device targets rebuilt after the shared-page update. See
[foreground receive sessions](linux-lora-receive-session.md).
Seven preceding
propagation upload/sync/stamp/encryption/RPC/routing/TMS ARM fixtures passed on
CM0. The preceding `de4fa2d...` installed formal
application passed real loopback TCP snapshot/upload/commit, actual exec,
configuration readback, receipt replay and orderly exit, with its configured
Auto IPv6 UDP sockets opened and replaced across exec. These checks used
temporary roots and dummy SDL devices without screenshots or user-data edits.
See [TMS implementation and boundaries](linux-tms-transaction-boundary.md).
The preceding AutoInterface delivery also passed five ARM transport/lifecycle
fixtures and formal-application ownership/replacement checks for its configured
IPv6 UDP sockets across exec. Public and protected peer interoperability passed
against official RNS 1.5.4 in isolated host network namespaces; see
[AutoInterface](linux-reticulum-auto.md). An external CM0 peer was not used.

The current propagation client is shared by generic Linux compositions. Official
RNS/LXMF host interoperability validates authenticated mailbox download and
durable-acceptance ordering, native recipient encryption, announced-cost stamp
generation, upload transport receipts and actual propagation-node storage.
The preceding native full regression passed 203/203 in one run and the rebuilt
compact suite passed 19/19. This included the optional gamma backend, explicit
wireless-interface selection and worker-owned saved-profile cycling. The
previous installed package (`de4fa2d...`) is retained as
`trailmate-cm0-before-probe-session.deb` in the device validation cache.
Those full suites were not repeated for the focused RF update.
The CM0 install used temporary application roots and dummy SDL without reboot,
display-driver changes or screenshots. The preceding package is retained for
rollback. See [propagation integration and evidence](linux-lxmf-propagation-client.md).

The preceding `505bdf6a...` package passed sixteen ARM pending-call path/key-discovery
scenarios and the existing ARM call/production-TCP fixtures.
Its real Wayland session was confirmed fullscreen/focused and exited on Alt+F4 with
code 0 in 0.586 seconds after the startup animation. The prior storage/menu
package passed its storage and four refresh-mode ARM fixtures; the dashboard's
RAM label updated to 230.8M/424.9M instead of retaining the initial empty sample.
The preceding memory-monitor package also passed its ARM fixture and displayed
cached RAM on the dashboard. The preceding screen-runtime
package passed ARM screen runtime/device-profile fixtures and rendered the P4 screen
saver preview and resumed homepage at 1232 x 568 in a real Wayland session.
Both validation windows were fullscreen/focused; Alt+F4 exited with code 0 in
0.433/0.434 seconds. These runs used a private fake `wlopm` executable confined
to the child application's PATH, leaving the real output state unchanged.
The preceding package passed 1232 x 568 Wayland
render and compositor-confirmed fullscreen/focused Alt+F4 exit from an active map
with code 0 in 0.639 seconds (the homepage exited in 0.649 seconds).
The installed executable SHA-256 is
`d64cf9ef42f2b2a59ee93679a8c83ff67d2d996b5154791087e0622704498f9b`.
Phone Bluetooth is intentionally disabled and deferred by user request;
the package no longer requires BlueZ or GIO. The preceding AutoInterface source
baseline covered 188 fixtures (187 in its full run and its updated capability
fixture separately); the current propagation source passed all 196 together.
Cardputer Zero passed 19/19 after both integrations. Earlier real process restart,
residual descriptor cleanup and clock success/rejection contracts also passed
on CM0 without changing its clock. Bluetooth experiments below are historical
evidence, not remaining requirements. Other ledger gaps remain incomplete.

The native strict settings ports now distinguish missing, invalid and
unavailable values. Application startup preserves a malformed working blob
and stops initialization instead of replacing it through first-start defaults
or old group migration. The installed application rejected an isolated damaged
blob with exit code 1 and retained its bytes. Four focused ARM fixtures passed;
see [strict settings reads](linux-strict-settings-reads.md). Native current-schema
TMS import/export is now connected through the document, transaction, frontend
and HostLink/web editor paths described below.

Native Wi-Fi now retains ten app-owned profiles through the shared snapshot
rules, stable SQLite reads and atomic replacement. The installed P4 form saved
two test credential pairs and retained them across a fresh process, while the
system Wi-Fi switch/active connections remained unchanged. Lookup, concurrent
saves, failure rollback and the reset fence passed in native and CM0 ARM
fixtures; see [profile storage](linux-wifi-profile-storage.md). Actual AP
switching remains unverified. The working source now implements worker-owned
saved-profile cycling, explicit interface selection and cancellation on manual
operations/interface changes. Its actual worker, interface selector and existing
parser/process fixtures passed on the host and CM0 ARM using simulated
NetworkManager operations. No actual AP switch was performed. These latest
network changes are included in the current installed package; see the profile
storage guide for the precise ownership and exhaustion behavior.

The installed P4 Contacts form accepted exact mixed-case/punctuation text,
saved it to isolated SQLite storage, and displayed it after a fresh application
process. Its saved target then opened the shared Chat compose screen and accepted
`Draft Ab!9` without sending. This investigation fixed legacy group reloads
overwriting edits and connected periodic configuration persistence to both Linux
loop styles; see [configuration ownership and verification](linux-config-persistence.md).

Generic network readiness now covers active Linux IP interfaces without assuming
Wi-Fi. Ordinary route images have native HTTP/HTTPS transfer, source integrity and
binding, JPEG/PNG decode and BMP cache support. The armhf fixture passed on CM0;
the installed Extensions page's generic Online state and keyboard filters passed.
Lintian reported no diagnostics. Physical Ethernet remains unavailable. Signed
installed-package image admission now verifies the original manifest, attachment
index/resources and installed KML before publishing images; its armhf fixture
passed on CM0. Signed route-package import/installation now also passes the
native, production-client and CM0 armhf workflows; see
[package installation](linux-hostlink-package-install.md) and
[signed route image admission](linux-signed-route-package-images.md).
The native remote extension repository
now supports catalog refresh/cache, verified archive download, atomic installation
and installed-state reload. The installed P4 UI read the official HTTPS catalog
and installed an isolated test package. See [network and route-image integration](linux-network-route-images.md)
and [remote pack repository](linux-remote-pack-repository.md).

The native HostLink audit found a legacy text handshake/echo implementation.
This has now been replaced by the shared binary frame protocol, Hello/status,
bounded short-write output and correlated command rejection. The real socket
fixture passed natively and on CM0. This is basic transport support, not the
complete management/route-install service; see [binary transport](linux-hostlink-binary.md).
The shared management envelope now supports capability query, author info/proof
and idempotent close (operations 0-3). Production web-client crypto verified the
native author proof over TCP streams, and armhf fixtures passed on CM0. This is
device key-possession proof, not client mutation authorization. Staged uploads
(32-36) now support durable chunks, identity/session ownership, recovery, hashing,
expiry and abort; C++ fixtures passed on CM0 and the existing production web
upload client passed over native TCP. Owned inventory and exact cleanup (37-38)
now pass native, production-client and CM0 armhf fixtures too; see
[inventory and cleanup](linux-hostlink-upload-maintenance.md). Public package
installation and fresh disk queries (100-101) are now implemented with original
author verification, immutable retention and record-before-KML publication.
An installed P4 UI test listed one real route rather than its binding metadata,
opened Route Preview, prepared its image cache to 1/1, and deleted the isolated
route while retaining signed cache data. See [package installation](linux-hostlink-package-install.md),
[author management](linux-hostlink-author-management.md)
and [staged uploads](linux-hostlink-uploads.md).
Failed listener shutdown
also reproduced a process abort from a joinable worker. Worker ownership,
serialized start/stop, descriptor cleanup and application shutdown are repaired;
the failure/exit/concurrency fixture passed on CM0. Physical USB management parity
remains unverified; see [HostLink lifecycle and scope](linux-hostlink-lifecycle.md).

## Open software parity observations, 22 October 2026

Native TMSET7 now shares base settings and compatibility codecs with ESP and
supports complete snapshot export, ten-profile Wi-Fi import, network records,
optional C6 data retention, atomic replacement and restart handoff. Protocol
compatibility records do not activate Bluetooth. Unsupported cellular records
and legacy native schema migration are explicitly rejected. The HostLink worker
marshals frontend work, persists resumable transfers and commits the receipt
with configuration under an atomic base-hash check. The existing production web
reader/editor/uploader passed native interoperation; the formal installed CM0
application passed real TCP and exec validation. This functionality is in the
current installed deb. See [TMS boundaries](linux-tms-transaction-boundary.md).

Native call media now has an owned PCM worker, shared Codec2 framing, bounded
jitter, audio focus lifecycle and P4 overlay integration. Eight focused host
regressions passed. The secure link owner also has an opt-in telephony
destination and identity-gated call payload ports, reusing its existing crypto.
The native protocol owner now combines shared LXST signalling, peer-directory
admission policy and media in a two-process fixture. Nine scenarios passed 15
consecutive instrumented host runs. The production mesh worker now owns a
serialized network/call component with shared IFAC, signed announcements,
endpoint/generation isolation and bounded I/O. Device-local services use it
even when physical LoRa is disabled. The call capability requires a connected
gateway, transmit permission, non-anonymous identity and media support.
Existing gateway host, port and policy settings persist through save/reload.
Eight final ARM fixtures passed on CM0, including actual application services
with LoRa disabled, durable text acknowledgements, bidirectional Codec2 over
TCP, independent endpoint retirement and one-shot asynchronous dial-failure UI.
Earlier native TCP repeated ten times and the combined call fixture five times
with sanitizers enabled. The native service API now persists and reloads a full
explicit network snapshot using shared TMS network records. Four new ARM
fixtures passed, including production TCP with an empty legacy gateway host.
HostLink/TMS passed 126/126 and the independent gateway suite passed 11/11.
See [configuration ownership](linux-reticulum-network-configuration.md).
The shared P4/compact multi-endpoint editor and factory-default policy are now
connected to native persistence. The worker defers and coalesces network edits
until a call's closing phase ends. Installed P4 pointer/keyboard input saved the
second endpoint as `CM0-MixedCase.example.test:5252` in an isolated settings root,
and a fresh process reopened it. Factory Reset now atomically resets native
settings, prevents old-process writes and suppresses discarded SD migration
sources. The installed P4 confirmation performed an app-only exec restart in an
isolated fixture, with defaults verified afterward; see
[factory reset](linux-settings-factory-reset.md). AutoInterface is now connected
and included in the installed package. LXMF propagation client flows are now
connected; the installed formal application received an official-node 40 KB
message into SQLite, preserved its complete author identity, acknowledged only
durably accepted transients and retained the message across application restart.
The restart also verified timed exit after repairing unbounded dashboard-preview
layout. See [propagation evidence](linux-lxmf-propagation-client.md).
Known-identity pending calls now wait for signed public keys/path discovery with bounded retry and a
real cancellable outgoing session; see [call path waiting](linux-pending-call-path.md).
Targets without identity information still require identity discovery before dialing. See
[native network transport](linux-reticulum-network-transport.md).
The shared call overlay also passed in the real Wayland session with injected
signalling and PCM; its clock-origin regression was reproduced and repaired.
The installed development package contains these changes. This is not proof
of an end-to-end peer call or physical acoustic performance.
See [native call audio](linux-call-audio.md).

The native diagnostics and shared P4 dashboard now distinguish an unconfigured
GNSS source from a configured receiver that has no data or no fix. The installed
baseline displays `GPS N/A` and `GNSS source not configured`, rather than asking
for a better sky view when no expansion/source is configured. Disabled, power,
missing-selector and stale-data cases passed native and CM0 ARM regressions.
This does not infer hardware absence from configuration or install the excluded
expansion. See [GNSS status readiness](linux-gps-status-readiness.md).

Linux now implements the shared navigation and place-search ports through
native workers and the existing TMAP `ArchiveCalculation`/search reader. ARM
fixtures passed on CM0, including a prohibited-turn detour, full archive
validation, publication/cancellation, Unicode search, ranking, region lookup
and media replacement. The installed P4 UI selected origin/destination through
the real place picker without GNSS, calculated four route points, saved the
archive and opened Route Preview with the detour polyline. The isolated fixture
initially contained no OSM raster tiles, so its preview correctly displayed missing tiles;
it also lacks a CJK font pack, so this does not verify Chinese glyph rendering.
Native TMAP raster/annotation consumption is now connected through the shared
tile source, annotation frame and P4 overlay. Native/ARM regressions verify RGB565,
RGBA/alpha layer order, cache/descriptor lifetime, CRC rejection/repair, file
events and a sparse-file read at a 3 GiB offset. The installed P4 Route Preview
reads the separate synthetic raster package, shows its detour, road label and
POI marker, and updates them after injected keyboard panning. This is not a
real-world map coverage/performance benchmark or physical key-matrix validation.
See [packaged map rendering](linux-packaged-maps.md) and
[offline navigation and search](linux-offline-navigation.md).
The shared Tracker
delete flow supplies absolute recording paths; the native adapter now accepts
that contract through rooted descriptor access. Recording also uses exclusive
filenames and the formal CSV/TMTRK1 formats. Native storage regressions and the
unchanged production browser converter passed; see
[recording storage](linux-tracker-storage.md). The ARM fixture passed on CM0,
including parsing the generated GPX with two points. The exact package above
passed Lintian without diagnostics, installation/integrity checks, original-logo
verification, P4 rendering and fullscreen Alt+F4. The prior development package
is retained as `trailmate-cm0-before-gps-readiness.deb` for rollback.
These findings and the hardware gaps below keep the overall adaptation incomplete.

**Subsequent target-ABI finding:** the new real-device resource-store test
exposed heap corruption with the generic Debian cross-build C++ ABI. This also
invalidates treating earlier startup-only package checks as proof of filesystem
runtime safety. The inspected Raspbian runtime uses mutex shared-pointer policy;
the generic cross headers used atomic policy. The same store contracts passed
on CM0 after using target-distribution headers and link runtime with GCC 14.
The application and all C++ dependencies were rebuilt in a separate target-ABI
directory. The resulting package above was installed and passed real-device
startup, external-locale discovery and Alt+F4 checks. The current installed
executable SHA-256 is
`43cb312f4faf90fdc2aed2ca77dcbc6dde1109dd10d580eb78d0616e435c7a91`.
The newly generated generic-ABI package was withheld, not installed; older
startup-only package checks do not establish equivalent ABI coverage.
Do not deploy the withheld package. See [target C++ ABI](linux-armhf-cxx-abi.md).

Linux resource-root composition now supplies the shared localization registry
from runtime data paths, with an explicit resource-root override. The installed
CM0 executable discovered a temporary external locale from a path containing
spaces, reached UI ready and exited successfully. This verifies resource
discovery. Actual local-directory installation, removal and snapshot failure
handling are now implemented and passed native tests. Both resource-store and
store-to-localization tests built by the corrected CMake toolchain passed on CM0,
including translated-text activation and removal, shared ownership, failure and
pre-publication process interruption. The installed app independently discovered
an external locale from an isolated directory containing spaces and reached UI
ready. The fresh target-ABI hardware probe returned battery available/99,
charging=0, external_power=1, keyboard present and Wi-Fi supported/connected.
Runtime and default package versions now follow CI-tag/Changelog precedence;
the current project version is 1.0.0. CM0 policy tests verified this exact runtime
version, SemVer ordering, memory-profile restrictions and captured-manifest
rejection/rollback. Policy facts come from each UI composition, and the storage
worker does not access LVGL. Local-directory installation and compatibility are
covered; remote catalog/archive delivery and large-uninstall UI latency remain open. See
[resource storage](linux-resource-pack-storage.md).

An earlier baseline deb had SHA-256
`7739bf3268fd6d738feeb3f5555d0994c005df85e25e0e9b4daddde0ada57485`.
It was installed successfully and passed package-file verification. Its first
post-install application check aborted during startup with PulseAudio's
`pa_mutex_free` mutex-destruction assertion. Three subsequent debugger launches
and ten ordinary launches exited normally. Those passes initially left the
intermittent defect unresolved. Subsequent three-thread pressure reproduced a
crash through the actual PCM input/output classes on the first run. A shared
SDL subsystem-lifecycle gate fixed that demonstrable concurrency defect: its
deterministic regression failed before the change and passed afterwards, the
real CM0 stress test passed three runs, and the full native suite passed 96/96.
The replacement package with SHA-256
`8f3115d7a0f007e8543e95dc389ad511c97bcc720832e3e2ff4406425ae8b176`
was installed and verified on CM0. Ten complete application startup/exit cycles
and a compositor-confirmed fullscreen Alt+F4 exit passed. See
[Linux SDL audio lifetime](linux-sdl-audio-lifetime.md) for scope and evidence.

## Ownership

Board configuration and Debian metadata live in `boards/t_display_cm0`.
`apps/linux_lvgl` composes services and the shared P4 touch UX.
`platform/linux/desktop` owns desktop display/input; Linux service adapters live
in `platform/linux/common`. Shared UI must not know a device ID or hardware path.
Do not upgrade ESP RadioLib merely to enable a new Linux radio backend.

## Device baseline, 20 October 2026

Read-only SSH inspection and bounded probes found Raspbian 13 Trixie armv7l,
kernel 6.18.50+rpt-rpi-v7, Labwc on Wayland, DSI-1 568 x 1232 at approximately
35 Hz, compositor transform 90 degrees. The app therefore uses zero additional
rotation. Optional keyboard presence is detected through evdev capabilities.
No system drivers were replaced during these probes.

| Capability | Authoritative observation | Trail Mate integration / remaining work |
| --- | --- | --- |
| Display | Labwc DSI-1 enabled; installed app rendered the P4 UI; compositor reported its window fullscreen and active | After RTC boot verification, Alt+F4 closed the current installed application with exit code 0 in 0.995 seconds. Physical touch/back gestures and hardware-key mapping still need validation |
| Touch | Goodix Berlin touchscreen, evdev event3 | SDL/Wayland pointer port implemented. A real installed-app Wayland navigation test exposed dropped same-pump press/release pairs; bounded pointer sample delivery fixes it, with mouse/finger regression coverage across four rotations. Repeating the injected click now opens Messages and its action popup. Physical coordinate/gesture test still required; see [pointer delivery](linux-sdl-pointer-input.md) |
| Keyboard | TCA8418 plus keyboard_lilygo evdev devices; app probe detected a keyboard | Automatic detection and SDL committed/preedit text input implemented. Installed Wayland Tab/Enter navigation, exact mixed-case/punctuation entry, form save/fresh-process reload and shared Chat compose input passed. Physical key-matrix mapping still requires validation; see [SDL text input](linux-sdl-text-input.md) and [configuration persistence](linux-config-persistence.md) |
| Battery | BQ27220 standard power_supply reports 99 percent | Background Linux power-supply monitor returns cached battery data without sysfs work on LVGL; actual armhf probe returned 99 on the device |
| Charger | BQ25896 USB power_supply online, Full; battery reports Not charging | Active charging is now distinct from external power. The updated actual-device probe returned `charging=0 external_power=1`; physical unplug/replug and active charging still require validation |
| Wi-Fi | wlan0 connected through NetworkManager | Stub replaced by real generic NetworkManager adapter; real status and fresh scan passed, one network found without disconnecting. Connect/toggle/error paths still need complete validation |
| Ethernet | RTL8152 USB adapter exists; fresh NetworkManager inspection still reports Ethernet unavailable | Generic background OS-interface readiness and system-routed HTTP replace the shared route-preview Wi-Fi gate. Wired-style/renamed-interface fixtures pass; physical wired-link validation remains outstanding |
| LoRa | LR2021 ROM 1.24, SPI0.0, GPIO7 reset/GPIO24 busy; two resets and 20 reads each at 1/2 MHz passed | Configurable LR2021 backend, generic packet port and mesh/foreground coordinator implemented. Chat and RF tools use that port. Remaining work includes protocol completeness review and audio integration; peer tests deferred by user |
| Audio | CM0-ES8390 ALSA card, ES8389 driver; capture, device selection and playback transport passed. A controlled probe identified DAC-to-ADC digital feedback; acoustic microphone response remains unverified. User confirms no external audio devices connected | Shared MP3 player, SDL PCM input/output, configurable Linux audio routes, notification chimes, volume/focus, Codec2/WT v2 walkie, SSTV receive/persistence and VMP workflows implemented. Native TCP calls now compose signalling and Codec2 media with injected-PCM regression coverage. External speaker/headphone acoustic verification does not apply to the current fitted setup. Microphone response, external-peer call interoperability and on-air verification remain unverified |
| IMU | User confirmed no QMI8658 expansion fitted; the image nevertheless loads its device-tree client | Excluded from fitted-device completion. Baseline does not expect IMU data and locks landscape orientation. Generic IIO backend remains available for an optional sensor |
| GNSS/LTE | User confirmed no NRF9151/GNSS expansion fitted; the USB dual-serial bridge alone does not establish a modem | Excluded from fitted-device completion. Baseline issues no SLM setup and owns no expansion power line. Configurable Nordic SLM, stable serial selection and pseudo-terminal tests remain for optional hardware |
| Camera | rpicam-still --list-cameras returned No cameras available | No camera detected; retain a runtime probe, connect camera to applicable SSTV/image workflows when fitted |
| Bluetooth | Historical authorized boot inspection exposed a powered controller | Intentionally disabled and deferred by user request. The current app does not start its phone runtime, even with legacy enablement settings. BlueZ/GIO are not application dependencies. System boot configuration was left unchanged during application cleanup; see [scope](linux-bluetooth-scope.md) |
| Brightness | Original image has no backlight class device; vendor panel writes fixed DCS 0x51 | Generic asynchronous adapter implemented. Experimental RM69A10 replacements passed compilation/callback checks but failed physical deployment, including a black/abnormal display. The original module and four boot images were restored and the user confirmed normal desktop recovery. Do not deploy the experimental patch; physical brightness/blanking/resume remains unresolved |
| Temperature/humidity | User confirmed AHT20 is fitted; earlier directed reads at 0x38 on I2C-1/2 did not respond | Explicitly excluded by the user on 21 October 2026: no temperature/humidity collection is required. Do not continue probe, driver or application integration work for AHT20. This exclusion does not assert that the sensor works |
| RTC | User confirmed RX8130; directed read at 0x32 returned valid time. Existing mainline driver exposes a valid RTC class with hctosys=1 | Packaged startup unit enabled and tested across an actual reboot. Boot journal reported a newly created client; kernel restored UTC from RTC at 18.525 seconds. RTC/system UTC difference was 0 seconds; the unprivileged app probe reported available/valid/system-source=1. Generic cached readiness and registration tests passed. Main-power removal/coin-cell retention and alarm wake are not established. See [RTC integration](linux-rtc-integration.md) |
| Storage/USB | System microSD and USB hub available | Configured data-directory readiness is cached from a worker, distinguishes regular files from readable directories, and separates writability/capacity. Map/track/pack/settings workflows have the evidence recorded below. Physical removable-storage and USB management interoperability remain unverified |

## Verification already performed

The 22 October fitted-hardware audit found an omitted keyboard-backlight path.
The vendor keyboard application implements Fn+F4/F5 using PWM. At audit time the
native device port returned false/zero and changed only an in-memory value in
its setter. A read-only CM0 probe found one
exported channel on the `pwm-gpio` device `/sys/devices/platform/pwm_gpio@16`:
period 50000 ns, duty 5000 ns, enabled, normal polarity. Its current class alias
is `pwmchip0`; a separate BCM hardware controller is `pwmchip1`. These aliases
are observations, not stable selectors. The vendor on setting and observed
state both use a 10 percent duty ratio. The new worker preserves that board
fact in the profile, selects the intended controller through its stable
device-tree identity and keeps hardware I/O off LVGL. Host regressions cover
class renaming, ambiguity rejection, unrelated-controller isolation, delayed
export attributes, normalized control and shutdown. The shared P4 menu's K
shortcut passed off/half/full cycling and app-scene isolation. An ARM fixture
on CM0 dimmed and disabled the keyboard light, then restored the exact initial
period/duty/enable tuple (50000/5000/1). This is separate from the unresolved
AMOLED brightness interface. The standard deb now includes the driver and
profile. Its installed profile passed the same ARM control/readback/restoration
test, package integrity/original-logo verification and the formal configuration,
actual-exec, receipt-replay and AutoInterface socket-replacement scenario.

The keyboard release gate ran the complete 198-test native suite. After restoring
incomplete local npm packages, its two browser-client failures exposed missing
AutoInterface capability export and Busy handling in configuration read/commit.
These were repaired and passed targeted native interoperation. Two new browser
contract tests also passed, bringing the aggregate native checks to 200. The
device-management bundle rebuilt using the unchanged locked dependency versions.
Cardputer's 19 checks passed after replacing a fixed delete delay with a bounded
wait for asynchronous completion and rerunning that case. These are combined
full-suite and targeted-retest results, not a claim of one new all-green run.

On 21 October, the standard `trailmate-cm0_0.1.38~alpha_armhf.deb` with SHA-256
`08a5ba38bd8160437a24cc560fb5d457054aa84d2f0f85ea898d89389982a380`
was uploaded and installed on the actual CM0 using `dpkg -i`. The package reached
`install ok installed`, and `dpkg -V trailmate-cm0` reported no mismatches. The
installed Logo hash matches `images/logo.png`; the desktop entry selects the
installed CM0 profile and `Icon=trailmate`.

The installed executable then ran in the real Wayland session with its normal
fullscreen profile, an explicit local runtime-mode override, and isolated
validation settings/cache. It reached `[BOOT][UI] ready`, saved a 1232 x 568 BMP
and exited successfully after the bounded application-loop interval. Visual
review confirmed the P4 menu/dashboard and production page entries. A subsequent
process check found no remaining application. This verifies real-device package
installation and application rendering/controlled exit, not physical touch/key
gestures, RF, GNSS fixes or microphone acoustics. Kernel modules
and boot configuration were not changed.

A subsequent compositor-level check ran the installed executable with the same
profile except for a unique validation title. `wlrctl` confirmed that this
window was fullscreen, then focused it and confirmed active/fullscreen state
before input. `wtype` sent Alt+F4 through Wayland's virtual keyboard. The process
exited with code 0 after 0.754 seconds, saved its exit screenshot and disappeared
from the compositor's toplevel query. Its 20-second automatic exit fallback had
not elapsed. No other application was running or targeted. This verifies the
installed application's compositor Alt+F4 close path, while physical keyboard
electrical/key mapping and touchscreen behavior remain separate checks.

The native Agenda store, product clock and background source component are now
implemented. They reuse the core event codec/service/query/scheduler and preserve
durable-write semantics behind a single worker owner. Frontend queries use
sealed read projections; accepted commands are drained on shutdown. Linux
timezone fallback uses thread-safe conversion and checked offset arithmetic.
The full native suite passed 81/81 checks, including the source's command,
projection-failure/recovery and real-page UI integration tests. The armhf backend
tests also passed under QEMU. The app entry point now creates this service and
binds Calendar's shared page/Map flow, dashboard and reminder popup. The native
waypoint worker now persists saved locations and binds the existing shared
waypoint picker, with durable completion and UI reads independent of SQLite. See
[Linux Agenda integration](linux-agenda-integration.md) for ownership, storage
failure semantics and the remaining composition requirements.

- Native and armhf builds succeeded after the Wi-Fi/input changes.
- Forty-one native tests passed: Linux/ESP P4 menu/page profiles, target bindings,
  device configuration, input rotations/Alt+F4, nmcli parsing and bounded command
  execution, radio ownership/power, music/audio focus, IIO, quick controls,
  application paths, native audio, generic Linux page availability, SLM command
  transactions, SLM serial lifecycle, passive NMEA compatibility and backlight
  discovery/selection/scaling/failure/readback behavior, SSTV PCM decoding and
  SSTV receive/persistence/lifecycle, WT v2 packet queues, Codec2 walkie sessions,
  native walkie lifecycle, FSK parameters/calibration, exclusive and legacy audio
  focus, persistent per-device node identities, and MeshCore group wire/adapter
  processing with independent AES/HMAC fixtures, persistent MeshCore identity,
  signed advertisements, active discovery scheduling/correlation, private wire/ACK
  processing with ASan/UBSan and independent crypto fixtures, and service
  protocol-ID switching, failed radio-configuration recovery, retry backoff,
  worker ownership/backpressure/cancellation, durable result dispatch and key
  verification runtime-failure presentation.
  The standalone SSTV core test
  uses ASan/UBSan. Cardputer Zero's independent build and eight tests also passed.
- A real armhf app process ran in the device Wayland session and exited zero.
- The real hardware probe returned battery available/99, keyboard present,
  NetworkManager supported and connected. It did not change network settings.
- A completed real Wi-Fi scan returned one network and the connection remained
  active afterward. SSIDs and credentials are not emitted by the probe.
- The vendor LR2021 probe passed without issuing RF transmit commands.

The new LR2021 backend also completed a bounded RX-only initialization on CM0:
driver online, 433.175 MHz settings accepted, instantaneous RSSI about -118 dBm,
and resources released. No transmit test was performed.

The user subsequently requested completing all software functionality first and
deferring peer-device verification. Do not wait for a second radio before
continuing development. Keep unverified on-air behavior explicit.

These observations do not prove the remaining hardware features. Keep the goal active.

## Native VMP session executor

`NativeVmpSession` now connects the shared session state machine, verified-contact
secret provider, control authentication, ephemeral key schedule, media/FEC code
and foreground RF lease. It is synchronous worker infrastructure, not an LVGL
callback or a thread scheduler. A service must own the clock/contact lifetimes,
cancel obsolete identity/protocol contexts before RF admission, persist an
outgoing object after preparation and before transmission, and bind UI summaries
only after durable storage.

Broadcast uses one CRC-protected ANNOUNCE, its declared delay, three readiness
probes and one fixed ten-shard train. Private send authenticates a matching
ACCEPT, including its complete binding/layout/PHY fields, derives fresh session
keys, then requires authenticated READY before media. Private receive verifies
the OFFER before generating an ephemeral reply or reserving RF. Readiness
responses are bounded and idempotent without extending the receive deadline.
Only authenticated private shards enter FEC; public objects remain explicitly
source-unverified. Two lost shards can be recovered, while insufficient or
invalid data produces no attachment. A receiver never retransmits media or
automatically plays it.

All waits check cancellation. Private replies also respect the ACCEPT window
after storage and RF setup, preventing late replies after a cancelled/expired
operation. One prepared send is consumed on every terminal outcome; there is no
automatic resend. Session keys, contact-key copies, ephemeral material, media
and packet scratch are cleared on completion/cancellation/failure. Restoration
is checked independently. A durable incoming commit remains valid even if
subsequent radio restoration fails; a service must preserve that attachment and
report the RF fault separately.

### One-time private control replies

VMP v1 authenticates controls using ChaCha20-Poly1305 with a derived control key
and a type/direction/session nonce. Replaying an OFFER and generating a different
ephemeral ACCEPT would reuse the same authentication key/nonce for different
authenticated bytes. This conflicts with the
[nonce-uniqueness requirement in RFC 8439](https://www.rfc-editor.org/rfc/rfc8439#section-4).
The native adapter therefore requires a durable reservation before generating
the first ACCEPT. Its callback receives only a SHA-256 identifier of the verified
control key, never the key itself. The default callback denies private receive.
Replayed reservations produce no new reply, including after a timeout, storage
failure, cancellation or restart. No radio wire format or algorithm changed.

Local snapshot schema 2 adds an independent append-only reservation journal,
sequence/count checkpoint and integrity chain. Startup and reservation verify
the journal. Deleting retained media does not delete nonce-use history. Missing
or altered schema-2 reservation data fails closed rather than being recreated.
The known schema-1 attachment-only format, which predates the native RF
responder, migrates transactionally while preserving media and IDs. This is not
an import path for an out-of-tree unfenced responder's history.

Reservations must be retained with the static contact-key lifecycle: erasing,
cloning into an independently responding device, or rolling them back while
retaining the same key schedule is unsupported. Retire/rotate and reverify the
corresponding identities before such a reset. The journal grows per authenticated
private OFFER and is scanned on the storage worker; a slow or unavailable
reservation causes failure/timeout rather than a late unsafe ACCEPT. Other
platform responders still require their own nonce-use handling; this native
guard does not change those implementations.

The ASan/UBSan fake-peer tests cover broadcast/private send and receive, wrong
ACCEPT bindings, absent peers, pre-RF contact/auth rejection, two-/three-shard
loss, tampered media, durable SQLite commit/reopen, storage refusal, replay after
reopen, cancellation during reservation and exhausted ACCEPT time. The full
native Linux suite has 68 passing tests, including the existing private-crypto,
control-auth, FEC and session-state regressions. The actual armhf session test
also passed under QEMU with fake RF, including the persistent replay guard.
No peer/on-air transmission test was performed. The application binding below
now owns the service mailbox, source context, mesh-worker sink and UI port.
Verified-contact key derivation and the native MeshCore verification handshake
are now connected as described below. Peer/on-air verification remains deferred.

## Application voice binding

Media-enabled real-mesh compositions now own `NativeVoiceService` in application
services. A compatible foreground radio is required: profile 1 capability is
queried explicitly, and an injected mesh port never borrows an unrelated global
foreground client. SX126x and media-disabled Cardputer compositions do not bind
this unsupported voice carrier. Linux UI hosts bind the existing shared chat
voice port through `VoiceRuntimeBinding`; no media or SQLite work runs in LVGL.

A single worker owns capture/playback, the attachment store, and each VMP
session. The frontend admits bounded jobs, returns cached metadata and supports
press-to-talk stop/cancel. An exclusive database-adjacent flock prevents another
application instance from repairing in-flight attachments. Protocol/identity
changes invalidate queued work. Incoming controls receive their protocol and
local node context from the actual mesh adapter's owner thread, and mismatched
contexts are rejected. Checked radio restoration remains part of completion.

Outgoing objects are committed as Sending before RF, then Sent or Failed.
Incoming objects are committed before the cached timeline is published. UI
wake failures cannot interrupt these durable transitions. Sent establishes
carrier acceptance, not remote delivery. Retained attachments support playback
and persisted read state without automatic playback, forwarding, or replay.
Hosts release the UI binding before services are destroyed, and shutdown joins
the mesh and voice workers. SDL per-thread state is explicitly cleaned up on
the std::thread exit path.

The integration test covers the public record/store/send/play path, an
independently prepared incoming peer object, local loopback deduplication,
read-state reopen, callback exceptions, competing ownership, immediate-stop and
context cancellation, responsive snapshots during blocked capture, and the
actual mesh-worker ingress bridge. It also checks application service binding
and rejects an injected mesh-only composition. These use fake RF and controlled
PCM. The application now has the verified-contact provider described below;
unverified contacts still fail closed. The physical
CM0 microphone's all-zero capture remains unresolved, and no acoustic or peer
interoperability claim follows from these software tests.

## Verified-contact voice keys

`LinuxVoiceContacts` publishes public trust snapshots from the existing peer
directory on its owner thread. MT requires a nonzero manually verified public
key; MC requires the existing verified-public-key flag and a consistent public
identity. Ignored peers, invalid/broadcast/self IDs, missing/zero keys, duplicate
IDs with conflicting keys, and unsupported identity families are refused.
Nicknames, discovery, channel keys and signed announcement reception do not
grant manual verification. Snapshot refresh never reads private keys.

Only the voice worker loads the existing local identity and derives VMP-only
`K_contact`. MT validates that the stored X25519 public/private pair matches;
MC reuses the existing Ed25519 identity conversion and checks the local node
binding. Both use the shared protocol-family and sorted-node-pair derivation.
Temporary private/shared/contact material is cleared. A concurrent trust change
invalidates the derived result before publication, and application services
cancel in-flight voice jobs on snapshot/context changes. MT and MC use separate
provider instances so a job cannot borrow the other protocol's contact state.

The mesh worker now publishes complete public keys alongside its existing
readiness snapshot. Application services synchronize them into the shared peer
directory without private-key reads or automatic verification. Existing manual
pins reject discovered replacement keys until the user explicitly revokes the
old pin. This also repairs a pre-existing Linux gap where the contact projection
had only a public-key-presence flag and no usable public-key bytes.
Incoming node observations also preserve the user's local ignore choice;
default `is_ignored=false` metadata can no longer undo local administration.

Native tests cover bilateral MT/MC derivation, local key mismatch, invalid keys,
manual trust, ignore/revocation, pinned-key replacement and a revocation racing
an expensive derivation. The application integration test seeds a real mesh key
store, hydrates the worker, checks full-key directory synchronization and
unverified refusal, then verifies that user verification admits a private job
and revocation cancels it. Full private fake-peer RF/session tests remain in
the separate session suite. No real RF peer test was performed.

MT's verification workflow and the MC handshake below are connected. An
unverified or ignored peer cannot supply private voice keys. Protocol-specific
branches prevent retained MT state from sending MT verification frames on MC.

## MeshCore contact verification

The native adapter now carries the production `TM` / kind 2 INIT, READY and
FINAL controls as authenticated direct appdata on port 12, for both MC V1 and
V2. An isolated `meshcore_wire::Verification` owns one bounded handshake; the
adapter owns identity derivation, radio operations and protocol-scoped UI events.
The existing number-entry/comparison presentation source and acceptance action
remain the user-visible verification workflow. Receiving FINAL never grants
directory trust automatically: the user must accept the comparison.

The six-digit number matches the ESP implementation: SHA256 over the 32-byte
identity shared secret, sorted little-endian 32-bit node IDs and little-endian
64-bit nonce, followed by the first little-endian 32 bits modulo 1000000. Native
nonce generation fails closed on CSPRNG failure. The session binds peer, public
key and nonce, rejects out-of-range/wrong numbers, and expires after 60 seconds
without extending its deadline for duplicate frames. A bounded received-INIT
history prevents completed/cancelled controls reopening prompts. Radio loss,
configuration/protocol changes, key replacement and failed transmissions cancel
the active session and publish a scoped failure. Controls do not enter the
generic chat appdata queue.

The shared MC padding helper now preserves the established fixed 12-byte
verification payload when its nonce ends in zero bytes. Both legacy and tagged
appdata prefixes are recognized, and bytes after the known payload must be zero
padding. Arbitrary ambiguous binary payloads still require a defined length
contract. This shared fix also covers the ESP receiver path without changing
the on-air format or upgrading the ESP radio library.

The complete native suite passed 71/71 checks. The MC verification test uses
ASan/UBSan/leak instrumentation on the actual adapter, wire codec, shared padding
helper, state machine and UI ports. It includes an independent hashlib fixture,
clock wrap/expiry, malformed controls, key/peer/nonce mismatch, encrypted V1/V2
initiator and receiver flows, user acceptance/rejection, cancellation and TX
failure. The armhf integration executable passed under QEMU with fake RF. The
media-disabled Cardputer build and 13/13 checks passed. These results do not
establish physical RF interoperability, acoustic readiness, or an ESP device
build; real-peer tests remain deferred as requested.

## VMP direct-RF plumbing

The generic FSK configuration now carries a bounded sync word and explicit
length, preserving WT v2's default two bytes. Backend-specific checks separate
SX126x limits from LR2021 limits. The latter follows the pinned RadioLib
implementation's supported bands, bitrate/deviation/bandwidth limits and
lower high-band output-power ceiling. `supportsFskConfiguration()` is a
side-effect-free capability query; unknown backends reject by default. Higher
layers do not identify a device by a chip-name string to select capabilities.

`VmpRadioLease` is a worker-owned foreground lease. Profile 1 uses the production
VMP channel map (2402-2480 MHz in 2 MHz steps), 500 kbps, 250 kHz deviation,
800 kHz requested bandwidth, 10 dBm, 16 preamble bits and the four-byte
`VMP\x01` sync word. It borrows the current Sub-GHz LoRa configuration for
control, then switches the same radio to the media profile. Configuration and
TX failures attempt restoration and release. The coordinator's checked release
also verifies any newer deferred mesh configuration, rather than equating
ownership release with successful restoration. A failed restore remains
reported as failed and the existing radio-recovery path retains responsibility
for bringing mesh RX back online.

The updated actual armhf RX-only probe ran on CM0 after checking for another
active radio application. It accepted profile 1 at 2402 MHz, reported the driver
online with instantaneous RSSI around -98 dBm, restored the Sub-GHz LoRa
configuration successfully and released the radio. It invoked no transmit
operation. This establishes configuration/RX startup only, not packet reception,
peer interoperability or voice delivery. Peer/on-air testing remains deferred.

Linux mesh receive now runs the existing core `ControlIngress` before generic
packet logging, raw monitoring and MT/MC parsing. VMP v1-shaped control envelopes
are consumed even when no voice sink is bound or the bounded sink refuses them.
Registered sinks must copy immediately into bounded worker storage and perform
no radio or UI work in the callback. Authentication and VMP session handling
remain the consumer's responsibility; classification itself establishes no
trust. Sink registration belongs to the mesh-adapter owner thread and its
lifetime must cover the registration.

ASan/UBSan tests exercise the foreground lease, latest mesh-config restoration,
partial FSK failure, TX failure and restore failure. The actual mesh-adapter
integration test covers MT and MC selection, malformed VMP controls, refused
sinks, absent sinks, signal metadata and unchanged ordinary raw monitoring.
Those plumbing checks originally brought the suite to 63 tests. The radio lease
and ingress now serve the application voice worker described above. Target
acoustic/on-air verification remains.

## VMP audio backend

Linux media builds now compile `NativeVoiceAudio`, a worker-owned capture and
playback adapter for the production VMP Codec2-1300 profile. The encoded clip is
bounded to 125 frames of seven bytes (875 bytes, five seconds at 8 kHz). The
adapter validates the linked codec's 320-sample/seven-byte geometry. It accepts
early stop only when complete frames exist, drops the unfinished tail, and
discards the entire clip on cancellation, input failure or ring overflow.
Temporary PCM is cleared on all return paths. Device opens and waits are
synchronous worker operations; callers must never invoke them from LVGL.

Both directions reserve exclusive VoiceMessage audio focus. Failed opens and
all terminal paths close the owned PCM stream and release focus. Playback
rejects other codecs and malformed lengths, uses bounded queue/drain waits,
applies requested volume together with the shared media gain, and supports
cancellation. Gain arithmetic stays signed so negative PCM samples are not
converted into large unsigned values.

The ASan/UBSan test uses partial-frame input, an independent Codec2 encoder for
the first-frame boundary, and independent decoder signal energy. Codec2's
internal random synthesis phase prevents a sample-identity oracle. The test
also covers the five-second frame ceiling, early stop before/after a complete
frame, capture cancellation, overflow, capture exceptions, failed opens,
playback failure/cancellation, global audio exclusion and zero-volume playback.
It additionally exercises the default SDL adapter with dummy audio through
the complete bounded recording and playback lifecycle. Early-stop tests use
controlled PCM so device initialization time is not mistaken for recording
time; stopping before the first full frame correctly yields cancellation.
The complete native Linux suite has 59 passing tests; the armhf media library
and application also build successfully.

This adapter now serves the bounded application voice worker and shared
`IVoiceMessageRuntime` bridge described above, together with the durable inbox
below. The real microphone's all-zero capture and peer validation remain
separate unresolved requirements.

## Local VMP attachment persistence

`LinuxVoiceStore` reuses the production `VoiceMessageInbox` for metadata,
direction/read state, eight-entry eviction and interrupted-send restoration.
Its APIs are worker-owned and have no transmit/forward operation. Incoming
objects must already have passed the VMP receiver's authentication policy;
local checksums are corruption detection, not a sender-authentication scheme.

The SQLite adapter commits one bounded canonical snapshot containing encoded
media, metadata, an allocation checkpoint and a generation. It uses explicit
integer byte order rather than serializing the C++ object layout. New entries
and status/read changes are prepared in a separate inbox and published only
after the transaction commits. Failed writes roll back without altering the
in-memory inbox. A stale generation, including an apparently duplicate request
from an outdated instance, fails rather than overwriting a newer snapshot.
Callers must quiesce an active carrier before cold hydration/reopening; storage
is not itself a process-wide voice-session ownership mechanism.

The core inbox now exposes its next local ID and accepts a validated allocation
checkpoint after restoration. This adds no fields or media buffers to the core
object. IDs consumed by deleted or evicted entries remain reserved across
restarts, including after deleting every retained message. The Linux adapter
refuses exhaustion instead of wrapping IDs. Terminal outgoing delivery states
cannot be changed back into a live send. Cold `Sending` entries are normalized
to `Failed` and committed before the restored inbox becomes available.

Snapshot version, length, flags, media geometry, conversation binding, CRC,
duplicate identity and the ID checkpoint are checked before restoration.
Missing committed rows, damaged snapshots and future schemas are retained for
review instead of being reset. SQLite value limits bound this connection's
payload allocation. Temporary candidate inboxes are cleared before release.

The ASan/UBSan persistence regression checks incoming/outgoing reopen, read and
delivery state, interrupted sends, exact duplicates versus conflicting media,
injected SQLite write failure, stale writers, delete-all/reopen ID continuity,
eviction order and corrupt/missing/future-schema preservation. The existing
core inbox regression also runs in the Linux suite. Together with the audio
checks the full native suite has 61 passing tests, and the armhf app builds.
This storage port is now owned by the application voice worker and projected
through the UI-summary binding described above.

## Native audio transport probe

The packaged `trailmate-linux-audio-probe --list-devices` enumerates SDL capture
and playback names without starting capture. `TRAIL_MATE_AUDIO_INPUT_DEVICE`
provides a common capture fallback below workflow-specific input selections;
`TRAIL_MATE_AUDIO_OUTPUT_DEVICE` selects playback for music, notifications and
voice. Empty/unset values use the session default, while unavailable explicit
names fail to open instead of falling back silently. No device name is written
into the shared defaults.

The route regression covers invalid names, explicit workflow precedence, valid
named capture/playback and restoration of the default route. Host and armhf
QEMU checks passed, and the same assertion-enabled executable passed on CM0's
real PulseAudio backend. The installed probe listed `Built-in Audio Stereo` for
both directions and returned 14,848 samples in one second at 15 kHz, with no
drops, clean stop and drained/stopped silent playback. The low sample amplitude
does not establish acoustic microphone readiness. The rebuilt native suite
passed 98/98. The updated standard deb was installed, verified and launched on
CM0; its SHA-256 is
`b3e9699d1075bb8d9ec1212afe3341a853500910c90cf1ed4f8d78eba2301731`.

`trailmate-linux-audio-probe --capture-ms 1000 --rate 15000 --play-silence`
uses the same PCM input/output ports as the application. Capture duration and
rate are explicit and bounded. `--input NAME` selects another SDL capture device;
otherwise the desktop's default route is used. The tool only publishes counts,
peak amplitude, dropped samples and lifecycle state. It retains no recording
and transmits no audio. Optional playback queues only digital silence and
checks that the queue drains and the device stops; it does not test acoustics.

On CM0, the actual armhf executable selected SDL's `pulseaudio` backend and
`alsa_input.platform-sound.stereo-fallback`. That source was unmuted at 100%.
The one-second 15 kHz probe returned 13,312 samples, zero dropped samples,
active capture followed by a clean stop, and a drained/stopped playback queue.
All captured samples were zero. A subsequent three-second capture returned
43,008 samples with the same all-zero result and no drops.

During that capture the ALSA hardware stream was S32_LE, stereo, 48 kHz, with
the four GPIOs routed to PCM clock/frame/input/output. ASoC's I2S OUT, ADCL,
ADCR and ADC MUX widgets were On and selected AMIC. The codec's reported stream
name is `I2S Capture`, while its DAI source uses `Capture` (and the playback
names differ similarly). Kernel DAPM documentation requires stream-name
alignment, but the observed live widgets were already powered, so this mismatch
is not established as the cause of silence. See the
[DAPM stream-widget contract](https://docs.kernel.org/sound/soc/dapm.html#stream-domain-widgets).
Further codec routing, input wiring and physical microphone checks remain
necessary. Digital transport success must not be described as working
microphone acoustics or speaker output.

A later differential probe found a very low-level signal rather than persistent
digital zero. Three two-second PulseAudio S32_LE stereo reads contained roughly
84,000-91,000 frames each; left peaks were 43,776-83,968 in 32-bit full scale,
and the channels did not substantially cancel when summed. The original ADC
digital gain was -28 dB. A bounded A/B probe temporarily changed only ADCL to
0 dB: two three-second application captures returned 33,373/44,544 and
40,687/44,544 nonzero S16 samples, with peaks 18 and 16, without drops. The raw
ADCL value 135 (-28 dB) was restored in a `finally` block and verified by mixer
readback. No recording was retained. This establishes a measurable capture path
and sensitivity to the existing mixer gain; it does not establish acoustic
response, a suitable production gain, or a fully functioning microphone.

On 22 October, the user confirmed that no external audio devices are connected.
The vendor README describes headphones and an expansion-board speaker, so no
external speaker or headphone acoustic result is expected for this setup.
This does not establish whether the board's microphone is physically populated.

A subsequent controlled probe played a one-second 997/1601 Hz digital stimulus
at a bounded per-stream volume while simultaneously measuring the actual default
capture source and playback monitor. The default capture source was the codec
input, not its monitor. With the original DAC-to-ADC mixer switch enabled, both
frequencies appeared in the left input at approximately 0.76 S16 amplitude;
the playback monitor measured approximately 314 S16 amplitude per tone.
Turning off `ADC Mixer DACL ADCL Mixer` reduced the input tone amplitudes below
0.004 S16, with a peak of one and effectively silent right channel. Changing
ADCL from its original -28 dB to 0 dB with the loop disabled did not restore
the tones. Re-enabling the loop at 0 dB restored approximately 0.75/0.76 S16
tone amplitudes. The matched test tones therefore demonstrate internal digital
feedback rather than microphone acoustic response. Earlier nonzero captures
must not be treated as microphone readiness evidence.

The probe's `finally` block restored the original mixer switch (`on`) and ADCL
raw value 135 (-28 dB); a separate SSH readback verified both. Global source
and sink volumes were unchanged. Temporary PCM recordings were deleted, and
only numeric measurements remain in the device validation cache as
`audio-loopback-compare.json`. No driver, boot configuration or production
audio defaults were changed. Microphone population, wiring and response remain
unresolved; raising the production gain is not justified by this result.

The supplied V0.3 schematic's audio sheet shows MIC1 ZTS6216 feeding ES8390 LIN1
through C194, with a separate 2.05 V microphone supply. The driver and live mixer
select AMIC/DifferentialL. This is insufficient evidence to replace the codec
driver or alter its default routes, and the tested device revision still needs
to be matched to the drawing. No kernel or boot changes were made in this probe.

The new bounded probe also runs with SDL dummy audio in CTest. The complete
native Linux suite now has 58 passing tests. Existing Cardputer audio capability
policy is unchanged; this CM0 result does not establish a compact-device audio
capability.

## Backlight driver deployment preparation

The panel callback review found a lifecycle defect in the earlier uninstalled
backlight patch: disable cleared its prepared flag, but enable did not restore
panel readiness or issue wake/display-on commands. The corrected patch separates
prepared and enabled states, initializes dark, leaves backlight enable to DRM,
wakes the panel again after disable, and rejects short brightness transfers.
The actual patched C callbacks passed ASan/UBSan tests for lifecycle transitions
and injected DSI failures. The corrected module was compiled on CM0 against
`6.18.50+rpt-rpi-v7`; its SHA-256 is
`7bf9753d0d6c51ad33cdccb13eb2941127fa63c52bd695218c38ced9c7787d4c`.

Read-only boot inspection confirmed the old DKMS panel module is also embedded
in the current kernel's initrd and firmware `initramfs7`. The original module,
current-kernel initrd and firmware initramfs images were copied to a validation
backup and verified byte-for-byte by SHA-256. Boot images and installed modules
remain unchanged. Because Labwc is actively using this panel, activating the
replacement requires an agreed reboot window, followed by class-interface,
brightness and blank/unblank checks. The deployment and rollback sequence is
documented in `boards/t_display_cm0/kernel/README.md`.

## Sensor and modem follow-up, 21 October 2026

The backlight deployment preparation above is historical. Subsequent authorized
installation/reboot attempts failed physical validation. The final state is the
restored original panel module and boot images, with normal desktop recovery
confirmed by the user. Compilation and callback tests did not establish a working
display driver. See the experimental warning and deployment/rollback evidence in
`boards/t_display_cm0/kernel/README.md`; the Debian package does not install or
activate this driver.

A fresh read-only inventory confirmed the QMI8658C device-tree client at
`1-006a`, but its driver is unbound after probe error `-5` (EIO), and there are
no IIO devices. The vendor overlay selects address `0x6a`. Its power-control
overlay exposes no separate IMU supply switch. These facts do not establish
that the sensor is absent; the physical bus/power fault remains unresolved.

Bluetooth was initially disabled by the image's `disable-bt` boot overlay.
The controller class was empty and BlueZ exposed no adapter object. GPIO14/15
were routed as the external
UART. GPIO debug names also expose the internal BT UART and BT_ON lines.
Removing the overlay alone is therefore not a verified Bluetooth integration;
the board's pin allocation and controller firmware/service must be handled
together. This inspection did not change boot configuration.

A subsequent inspection found the existing `hci_uart.ko.xz`, CM0 Bluetooth
firmware and the original CM0 device tree's enabled Broadcom Bluetooth node.
The base tree assigns UART0 to internal GPIO30-33 and BT shutdown to GPIO42.
The disable overlay currently leaves GPIO30-33 idle and routes UART0 to 14/15.
The raw boot command line uses `console=serial0,115200`; its current kernel
expansion is `ttyAMA0`. The UART getty link is generated under `/run/systemd`,
with no persistent ttyAMA0-specific override found. Consequently, the reviewed
candidate only comments the Bluetooth-disable overlay and preserves the primary
UART alias and all other boot settings. Official UART/overlay behavior is
described in the [Raspberry Pi configuration documentation](https://www.raspberrypi.com/documentation/computers/configuration.html#configure-device-tree-overlays-for-uarts).

The original configuration SHA-256 was
`5c897e0164ec810b97a74868f207755061505a988e14f1e2ac9911c4cd28f769`.
The prepared candidate is
`df39ff451a77ea50954dbefb2d3d9c2f6506ae3267b4c4915a0598cf50db68b3`.
The command-line file remains
`10683e97c686330ea833388c1f65331e4ab561296bc2051f5ecac95d921d702b`.
Original files were backed up and verified under
`/root/trailmate-bluetooth-deployment`. The explicit apply/rollback helper checks
the model and reviewed hashes, preserves bytes/newlines, and replaces the file
atomically. The user subsequently authorized enabling Bluetooth and rebooting.
Only the reviewed config line was changed, and the candidate hash became active;
the command-line file was unchanged. The controller then appeared, loaded the
existing `BCM4343A2.raspberrypi,0-compute-module.hcd` firmware, and exposed both
GATT and LE advertising managers. The controller-specific software rfkill entry
was unblocked; BlueZ subsequently reported `Powered=yes` and `PowerState=on`.
An immediate concurrent power-set request failed during that transition; readback
confirmed the powered state without another power cycle. There are zero active
advertising instances. The native discovery library queried the real system bus
and selected the controller as Ready without a hardcoded controller path.

Wi-Fi reconnected and Labwc resumed with DSI-1 enabled and transform 90. The
original display module and all four original boot images were verified
unchanged. The installed application started fullscreen. The initial SSH-driven
exit checks did not obtain active focus and sent no closing key. After an
Escape/activation step in a verified single-validation-window session, the
compositor reported active/fullscreen and Alt+F4 exited with code 0 in 0.448
seconds. No application process remained. This OS bring-up does not provide
the still-missing GATT/phone-session application binding.

The initial Linux BlueZ discovery/selection library is now implemented separately
from the phone protocol owners. It queries real D-Bus object-manager data,
selects by full controller address or one unambiguous eligible controller, and
does not assume `hci0`. Host and CM0 armhf private-bus tests passed, including a
peer that never completes authentication and is cancelled at two seconds. The
library is now used by a separate GATT transport. Its Meshtastic and MeshCore
service/advertisement registration probes both passed on the actual CM0 daemon,
as did its armhf private-bus contract tests. Closing the dedicated connection
released the advertisements and left ActiveInstances at zero. The host full
suite subsequently passed 105/105 after binding shared phone sessions and the
application service lifecycle. CM0 passed the armhf application-owned private-bus
test, including owner configuration, durable reopen, disconnect and shutdown.
The new alpha package was installed and verified as recorded above. Pairing
policy and native MeshCore backend operations remain unfinished; no physical
phone interoperability or peer-radio claim follows from the private-bus tests.
See [Linux BlueZ adapter discovery](linux-bluez-adapters.md) and
[Linux BlueZ phone GATT transport](linux-bluez-phone-gatt.md).
The ownership and remaining administration scope are detailed in
[Linux phone runtime](linux-phone-runtime.md).

The vendor sample owns GPIO12 as an active-high `nrf9151_ctl` enable, whereas
the current Trail Mate SLM source only handles serial settings and commands.
On this device GPIO12 was unclaimed, configured as input and low. A bounded
probe acquired an exclusive libgpiod output lease, drove the vendor enable
high, waited 1.5 seconds and sent only `AT` at 115200 baud on each CH343 serial
channel with DTR/RTS asserted. Neither channel returned bytes within its
two-second response window. The lease was then released and GPIO12 restored
to its observed input state. The test did not establish modem firmware or
readiness; the startup window may also be insufficient for some firmware.

The GNSS integration now includes an optional configurable power-line owner
and nonblocking startup delay before serial protocol setup. The earlier optional
CM0 expansion profile selected GPIO12 with an active-high enable and a two-second
startup gate; the current baseline leaves that line unclaimed. Generic
Linux defaults do not own a power line. `gps_power_gpiochip`, `gps_power_line`,
`gps_power_active_low` and `gps_power_startup_ms` describe other wiring; setting
the line to `-1` disables ownership without deleting reusable wiring values.
GNSS power and radio lines on the same chip must be distinct.

The optional CM0 expansion serial selector supports the CH343 interface's stable by-id filename
pattern instead of a fixed `ttyACM0` number. Resolution occurs at serial open,
with one absolute directory, a 256-entry scan ceiling and exactly one matching
character device. Zero matches remain unavailable for hotplug retry; multiple
matches fail with ambiguity rather than selecting the first device. An exact
by-id path or a user-managed udev alias selects one of several identical
expansions without putting a unit serial number into the shared profile. Host
pseudo-terminal tests cover stable matching, missing devices, ambiguity and
interface separation through both NMEA and SLM transactions. This improves
device selection but does not establish a physical modem response or GNSS fix.

The selector's NMEA, SLM and powered-SLM pseudo-terminal tests also passed in
assertion-enabled armhf executables under QEMU. The rebuilt standard deb passed
extracted-application bootstrap/rendering checks, and its installed device
configuration matches the stable-selector profile byte-for-byte. This does not
replace the outstanding physical modem and sensor investigation.

`AsyncGpioPower` owns the GPIO work on its thread. Requests coalesce without
holding the UI mailbox across a hardware call. It requests the active level
atomically, waits for the configured startup period, and only then permits the
serial runtime to open the modem and start AT setup. Failed acquisition is
reported and retried with a bounded backoff. Serial failures preserve the
power lease, while disabling or suspending closes the serial source, requests
inactive power and releases the line. A late enable completion cannot publish
readiness after cancellation. Inactive-write failure remains a fault instead
of being reported as successfully off.

The standalone `trailmate-linux-gnss-power-probe DEVICE_PROFILE` exercises the
real GPIO backend without serial commands or RF transmission. The rebuilt armhf
probe ran as the ordinary CM0 user and returned
`gnss_power_ready=1 error=0` followed by `gnss_power_released=1 error=0`.
The pin was observed low after release. The kernel only guarantees requested
line state while the request descriptor is held; released lines may return to
their platform default, as documented by the
[GPIO v2 API](https://docs.kernel.org/userspace-api/gpio/gpio-v2-get-line-ioctl.html).

The ASan/UBSan power-worker test covers a blocked acquisition, cancellation,
startup delay, idempotent requests, busy retry, shutdown and failed release.
The serial integration test uses the actual GPS runtime with a pseudo-terminal:
it checks the startup gate, missing-serial retries without a power cycle,
AT setup after reconnect, disable/re-enable and suspend. The complete native
Linux suite now has 57 passing tests. The current lack of real AT response
remains a separate physical or firmware readiness issue; a successful power
probe does not establish GNSS operation or a location fix.

## Battery and external power correction

The P4 IDF `handle_low_battery` port is currently a no-op, like the Linux port;
the Arduino implementation has a separate board-specific policy. The Linux
adapter therefore does not lack a P4 automatic shutdown action. No Linux OS
power-off policy was introduced by this audit.

The generic Linux adapter now reads standard power-supply properties on its own
thread. It does not bind to BQ chip names. `TRAIL_MATE_POWER_SUPPLY_ROOT` can
replace the class root; `TRAIL_MATE_BATTERY_DEVICE` selects a named system pack
when automatic deterministic selection is inappropriate. Device-scoped
peripheral batteries are excluded. Missing or invalid capacity is unavailable,
not a fabricated percentage. All online external supplies are considered, so
an offline first entry cannot hide a second online source.

`BatteryInfo.external_power` is separate from active `charging`. For a single
system pack with one online charger reporting a phase, that phase can supply
information its separate gauge lacks. With multiple packs or ambiguous charger
statuses, no cross-pack phase inference is made. Full and Not charging never
mean active charging. The menu keeps the percentage and adds USB for external
power without charging; its unavailable-battery and dashboard states also
recognize external power independently.

A bounded real-device probe of the rebuilt armhf adapter returned
`battery_available=1 battery_level=99 charging=0 external_power=1`, with keyboard
and NetworkManager still present. The probe made no power or network changes.
The new ASan/UBSan test checks charger phase, disconnected power, multiple
supplies and packs, peripheral exclusion, malformed capacities and a deliberately
blocked filesystem reader while UI snapshots remain responsive. The native
Linux suite now has 55 passing tests. Physical charge transitions and remaining
peripheral readiness are not established by this result.

The updated armhf package was built after these software changes. Lintian and
desktop-file validation passed; its icon matches the existing repository logo.
The extracted ARM executable ran under QEMU with dummy SDL video/audio, rendered
a bounded capture, and exited zero. This does not establish physical audio or
the remaining hardware functionality. The package is still a development build.

## Packet radio development

`IPacketRadio` isolates the Linux services from chip commands. Existing modulation
and packet structs retain their ABI through aliases to avoid a broad wire-model
rewrite. `PacketRadioCoordinator` opens one driver lease, pauses mesh I/O while a
foreground RF tool owns the radio, defers mesh parameter changes, and restores
the latest mesh configuration on release. A failed restore makes mesh offline.
The raw chat adapter now balances acquisition on configuration failure and exit.

The LR2021 backend uses a Linux-only RadioLib target pinned by commit and archive
SHA-256. Its HAL owns spidev and gpiochip line descriptors, kernel chip select,
an advisory SPI lock, bounded BUSY handling, and reset/power cleanup. Hardware
paths, pins, SPI speed and TCXO voltage come from the device profile.
ESP RadioLib and other Linux compositions do not enable this target implicitly.

Tests cover foreground exclusion, deferred configuration, restoration failure,
balanced physical leases, disabled-driver behavior, missing-device failures, and
the existing UI/network/configuration checks. The expanded suite now has 24
passing tests, including music playback/focus, IIO, quick controls, XDG paths,
and generic Linux page availability without a device macro. A separate
compile/run with LR2021 disabled also passed the coordinator tests.

## Storage readiness

The Linux device port previously treated any existing `TRAIL_MATE_SD_ROOT`
path as ready, including a regular file, and could create directories from a UI
getter. `StorageMonitor` now performs root resolution, directory creation,
directory/read/search checks and filesystem-space sampling on its own worker.
`sd_ready` and `card_ready` only read the cached availability flag. Initial
discovery is unavailable until the worker publishes its first valid sample.

Availability describes the app's configured logical data directory, not proof
of a separately mounted or removable SD device. A readable read-only directory
remains available for maps/music. Writability and known capacity/available bytes
are separate snapshot facts; they do not guarantee a future write will succeed.
Paths and mount layout come from the runtime directory configuration, without
CM0 device names. An unreadable/non-directory root or a thrown probe clears
availability, and later successful probes recover it. A failed space query
leaves capacity unknown without hiding an otherwise readable directory.

File-operation ports do not use that cached UI flag as authorization to perform
I/O. Reticulum group loading/saving and announce-file operations probe their
actual root in their existing blocking I/O context. This distinction is required
for startup migration: treating the monitor's initial unknown state as absence
skipped legacy groups in the settings/reset regressions. Those existing tests
exposed the regression before installation, and the operation ports were
corrected instead of adding timing delays to tests or startup.

The fixture covers a regular file passed through the actual device ports in a
fresh process, a non-directory parent, missing-directory creation, permissions,
capacity, replacing a directory with a file and recovering it, and 10,000
snapshot reads while an injected probe is blocked. Physical removal of the
system's boot microSD and USB storage interoperability are not implied.

The final storage changes passed all 158 native tests, including the ASan/UBSan
storage fixture and the existing migration/reset regressions, plus the rebuilt
19/19 Cardputer Zero suite and actual ARM fixture on CM0. The ESP stack guard
passed. The conditional non-Linux fallback compiled with Linux-only code disabled;
this is not a native Windows runtime test. The final armhf deb passed Lintian
without diagnostics, installed with intact original-logo and `dpkg -V` checks,
and exited a focused/fullscreen real Wayland session through Alt+F4 in 0.530
seconds with code 0. No display, radio, audio or mount configuration was changed.

## Memory sampling

Linux memory statistics now use `MemoryMonitor`. The shared device port returns
a mutex-protected cached snapshot instead of opening `/proc/meminfo` twice on
the LVGL thread. One worker reads total and available RAM from the same bounded
sample at a one-second interval. Missing, duplicate, malformed or overflowing
values invalidate the complete sample; available memory cannot exceed total.
The adapter does not fabricate PSRAM. Until an initial sample is ready, or
after a failed read, the port returns zero/unavailable statistics.

The native fixture parses valid and invalid documents, including zero available
RAM and a final line without a newline, exercises the target's `size_t` overflow
boundary, blocks an injected sampler while making 10,000 cached reads, verifies
publication after release and clearing after a subsequent exception, and reads
the actual Linux device port. This removes the known synchronous memory-file
I/O path; subsequent presentation integration is described below.

A later installed-app screenshot exposed a first-sample presentation race:
the menu initially displayed an empty cached sample and its default 60-second
battery timer delayed memory refresh. The generic menu/startup hooks now carry
a bounded status refresh interval (1-60 seconds), with the existing default of
60 seconds preserved. The Linux LVGL composition selects one second. Bottom-bar
refresh is performed by the status timer independently of battery availability,
so a Linux device without a battery still receives RAM updates. Dedicated real
LVGL fixtures use a controlled clock to verify default, cached, lower-bound and
upper-bound intervals, including a deliberately pending RAM label and an absent
battery. This change is a composition setting, not a Linux condition inside the
shared UI. The complete native suite then passed 162/162, Cardputer Zero passed
19/19, and all four refresh-mode fixtures passed as real ARM executables on CM0.
The installed package's post-startup screenshot was visually inspected and
showed 230.8M/424.9M RAM, confirming that initial background discovery reaches
the actual dashboard. Its focused/fullscreen Alt+F4 exit returned code 0 in
0.499 seconds. Package integrity, original icon and Lintian checks passed.

Validation on 22 October passed the native complete suite (157/157), including
the ASan/UBSan-instrumented memory fixture, the rebuilt Cardputer Zero suite
(19/19), and the actual ARM fixture on CM0. The new standard armhf deb passed
Lintian without diagnostics, was installed and verified with `dpkg -V`, retained
the original logo and `screen_power=none`, and exited from a focused fullscreen
Wayland window through Alt+F4 with code 0 in 0.449 seconds after startup. The
captured dashboard was visually inspected and showed approximately 212.8M used
of 424.9M total RAM. An earlier exit during the startup overlay took 0.782
seconds and was not used as dashboard evidence. These are app/runtime
checks; physical battery unplug/replug remains unverified.

## Native audio development

The explicit Linux media composition now includes `SdlPcmInput` and a native
notification worker. Both use SDL's default session devices; no ALSA card index
or CM0 codec name is embedded in shared code. Capture opens only after `begin`,
requests signed 16-bit mono PCM at 8-48 kHz, and stores at most one second in a
fixed ring. Slow readers discard the oldest samples with an explicit drop
counter. Audio callbacks do not allocate or access files. End closes the device
before releasing its callback storage.

Message notification requests are asynchronous and coalesce to one pending
chime. A chime reserves notification focus, waits for music to close its PCM
device, plays bounded generated PCM, closes the device, and releases focus.
Existing call/voice/RF audio focus suppresses the chime. The settings preview and
incoming-chat bridge use this path. The saved `speaker_volume` is restored on
desktop startup and shared media volume changes also apply to alerts.

The application joins notification audio before shutting down music and SDL.
The audio-disabled composition retains explicit unavailable/no-op ports and
does not gain an SDL dependency. Device enumeration is availability evidence,
not proof of a working physical microphone or speaker.

Native tests cover dummy PCM capture, bounded overflow, invalid/missing input,
recovery, mute and gain synchronization, notification focus and shutdown. Music
integration also exercises a real core MP3 player interrupted by the native
notification worker. These are host software tests, not acoustic or on-air
evidence. VMP and walkie now consume the ports through their worker-owned
services; call integration remains incomplete. SSTV consumes the capture port through the shared DSP and a
native service, as detailed below.

## GNSS development

`GnssSourceOptions` belongs to the Linux composition. `nmea` remains the passive
default for existing Linux targets and for the current CM0 baseline without an
expansion. A user-owned profile for a fitted NRF9151 can select `nordic_slm`,
115200 baud, optional DTR/RTS assertion and the GNSS-only modem setup policy.
Use an exact stable udev path or an unambiguous by-id filename pattern. Shared
code has no CM0 serial path.

The nonblocking SLM controller sends one command at a time, handles partial
writes, requires acknowledgements, times out command transactions, and retries
failed initialization after a backoff. It uses continuous navigation with
`AT#XGPS=1,0,1`; it does not stop after the first fix as the vendor demo does.
The explicit GNSS-only setup disables cellular operation, matching that demo.
Leave `gps_gnss_only_setup=false` when another modem owner manages LTE/GNSS
coexistence. A GPS-only adapter is not an LTE/telephone service implementation.

The [Nordic SLM GNSS contract](https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/applications/serial_lte_modem/doc/GNSS_AT_commands.html)
defines speed in m/s and altitude above the WGS-84 ellipsoid. The adapter validates
coordinates, finite measurements and timestamp syntax/calendar fields. It
publishes no invented satellite count or HDOP and performs no implied MSL or
UTC clock conversion. Position, speed and course enter the existing shared GPS
state used by map, tracker and team sampling. Readiness stays false until SLM
initialization is acknowledged, while fix validity remains a separate state.

Serial access is nonblocking, close-on-exec and cooperatively locked for SLM.
Receive work per service tick is bounded; overlong records are discarded through
the next line boundary. Disabling GPS or shutting down initialized app services
requests a best-effort GNSS stop and closes the descriptor. This does not prove
modem power-off. The existing passive NMEA path does not transmit AT commands.

Tests exercise command ordering/echo/errors/deadlines/partial writes, bad
coordinates and timestamps, and actual POSIX pseudo-terminal lifecycle through
the shared GPS service. They verify accepted positions, readiness diagnostics,
disable/restart and passive NMEA behavior. Earlier physical AT queries did not
receive responses; the software implementation does not close that hardware gap.

## Backlight development

Read-only checks against the restored stock image on 22 October found no
`/sys/class/backlight` entries. DRM reported the DSI connector connected and
enabled, with DPMS On; HDMI was disconnected. The session's `wlr-randr` reported
one enabled DSI output, preferred/current mode 568 x 1232 at approximately 35 Hz,
transform 90, scale 1. `wlopm` successfully queried that output as `on`.
These are discovered device/session properties, not identifiers to hardcode in
the generic adapter. No output power changes were performed. Query success
does not prove panel disable/enable recovery or provide brightness control.

A subsequent stock-driver session probe armed an independent delayed recovery
process before requesting a roughly half-second power-off interval. Compositor
queries returned `on`, `off`, then `on`, and both ordinary and independent
restoration commands completed successfully. The user could not observe the
physical display result, so these queries do not establish optical recovery.
A new independent
Linux `OutputPowerRuntime` implements worker-owned discovery, unambiguous or
explicit selection, requests bound to the observed output, readback, failed-sleep
recovery and shutdown restoration. It is connected to the LVGL app screen
lifecycle through an explicitly selected `screen_power=wayland` profile, and
does not expose brightness. CM0 still ships with `screen_power=none` while
physical recovery remains unconfirmed. See [output power](linux-output-power.md).

The earlier Linux screen-runtime gap is now repaired: state-machine access is
serialized, background callers only update state, and the UI loop reconciles
hardware and applies the shared saver hooks. The SDL input observer consumes
wake input before page/editor delivery, and keyboard confirmation or an upward
swipe resumes the preserved page. Existing nested call/tool sleep inhibitors
use this same state machine. Missing or failed power control releases logical
sleep and restores owned power when available; shutdown joins the restoring
worker before UI deletion. The real SDL/production worker fixture passed ten
consecutive ASan/UBSan runs and executed successfully on CM0. This software
evidence does not establish stock-panel physical recovery or brightness.

The installed-package session fixture exercised actual P4 rendering with a
private compositor-power simulator, isolated settings/cache/data directories
and no radio backend. Both runs used a persisted 10-second timeout, observed the
application's Off request, sent the initial wake key and observed its On request.
One exited while the shared saver was visible; the other confirmed resume with
SPACE and exited on the homepage. Both captured BMPs were visually inspected:
the preview showed time, unread count and the SPACE resume hint; the resumed
image showed the complete P4 touch dashboard. The fake recorded exactly two
Off/On pairs across both runs, and real `wlopm` queries matched before/after.
The fixture's `report.json`, logs and screenshots remain under the device's
`screen-runtime-ui` validation cache. This is end-to-end app evidence with a
simulated power port, not a second physical panel recovery test. Native full
regression passed 156/156; Cardputer Zero's rebuilt suite passed 19/19.

`BacklightRuntime` owns sysfs discovery, coalesced writes, readback and optional
logind calls on a worker. UI getters read only cached state. It normalizes native
ranges to 0-255, prefers `actual_brightness` when available, observes external
changes, and refuses automatic selection when more than one valid backlight is
present. A profile can select an absolute device path. Errors do not become
optimistic brightness updates; quick controls surface failures/selection needs.

The CM0-specific kernel patch is isolated under `boards/t_display_cm0/kernel`.
It adds a standard raw backlight to the vendor RM69A10 driver and serializes DCS
brightness updates with panel lifecycle state. The patched source compiled on
CM0 against the installed `6.18.50+rpt-rpi-v7` headers, producing an ARMv7 module
with matching vermagic. No driver installation, module reload or reboot was
performed. The app remains unavailable for brightness on the original image
until a backlight interface is deployed. See the kernel README for exact build
evidence and physical-validation limits.

## SSTV development

The pure C++ decoder, half-band filter and CORDIC moved from the ESP platform
directory to `modules/core_sstv`. ESP-IDF source ownership and PlatformIO module
dependencies select the shared owner; Linux does not compile an ESP board
service. Decoder smoothing state is instance-owned, and CORDIC tables retain the
original quantized values as immutable data. A Pager SX1262 PlatformIO build
completed successfully after the extraction and final DSP updates.

The native service opens the default or explicitly selected SDL microphone at
15 kHz, holds SSTV audio focus, feeds bounded PCM blocks, and publishes coherent
image copies/status to the existing shared page. Capture overruns reset scan
acquisition. Stop/restart generation IDs prevent stale workers from publishing
over a new receive operation. PCM decode and BMP persistence run off the UI
thread; shutdown joins capture before music/SDL teardown. The old timed image
generator is removed. Audio-disabled Linux compositions report unsupported.

Completed images use unique BMP filenames beneath the shared SD root's `sstv`
directory, or the explicit output override. Getters perform no filesystem or
device work. The framebuffer returned to the UI is an owner-thread copy, not
memory concurrently modified by the decoder.

Tests generate an independent BW8 PCM scan train and check acquisition, decoded
luminance, complete state, block-size/reset independence, native service image
publication, BMP persistence, missing input, silent capture, overlap rejection,
stop and focus release. The core test runs with ASan/UBSan. The optional raw PCM
file source is explicit, little-endian signed 16-bit mono at 15000 Hz; incomplete
files error rather than becoming completed images.

The ARM file-input probe also decoded the same PCM on CM0 and saved a BMP.
Its SHA-256 matched the native result:
`79a39a84d5df2644a37b949afaa6098c480eb0c6a5b1767cd963afa6ac45e9bb`.
This exercised target CPU decode/storage using an explicit offline source, not
the physical microphone or an on-air signal. The shared page now reacquires its
frame snapshot on each refresh, allowing owner-thread copies to advance after
the initial image descriptor was created.

The inherited DSP acquires mode from scan-sync cadence and does not use VIS.
Acquisition can omit initial rows even when the input contains a header; VIS
and first-row alignment remain an implementation/quality gap. BW8 host
waveforms do not prove every inherited mode or noisy/on-air reception. Acoustic
and peer-device checks remain deferred; keep these limitations explicit.

## Radio configuration recovery

The coordinator tracks whether the physical modem configuration is known valid,
independently of a backend's cached online flag. LoRa/FSK configuration or receive
setup failure invalidates online/statistics state and blocks packet I/O and RSSI
reads. Configuration operations remain available to recover; a successful known
configuration or mesh restore clears the failure. Foreground FSK can therefore
recover without bypassing lease/power checks. Mesh restore failures remain
offline until recovery, even if the backend itself leaves `isOnline()` true.

The raw adapter also clears its own started state and releases its lease after
a failed settings change. It explicitly starts RX after configuration rather
than relying on a particular chip backend to do so. Initialization failures and
missing endpoints use a nonblocking exponential retry gate, initially 250 ms
and capped at five seconds. The cooldown starts after the failed operation has
finished. Explicit settings application resets the gate for immediate recovery;
successful initialization resets the delay. Cached readiness/status access does
not initiate an acquisition. Existing constructor entry points remain available;
an injected monotonic clock makes retry timing deterministic in tests.

Tests keep the fake backend online after failure, assert that no packet I/O
reaches it, recover through LoRa/FSK configuration, reject failed RX startup and
exercise failure/recovery lease balance. A fake clock advances during a failed
acquisition to verify that slow I/O cannot consume its own cooldown. Pure gate
tests cover increasing delays, the cap, reset and clock wrap. This prevents
repeated resets on every UI iteration. Steady-state mesh calls now use the worker
below. Direct power-control and foreground RF-tool entry still need latency work.

## Mesh worker and durable outcomes

`LinuxMeshWorker` is the application-facing `IMeshAdapter`. One worker owns the
synchronous `LinuxRawLoraMeshAdapter`, including SPI/GPIO access, protocol crypto,
peer-key persistence and receive polling. The UI submits bounded commands and
reads snapshots. Startup may wait for protocol-identity bootstrap, without taking
a hardware lease; normal configuration/send/poll/getter paths do not wait for
hardware. MeshCore's ID is preloaded for immediate protocol switching. Generic
service composition can inject an owned radio port; no device ID/pin decision
belongs to the worker. The existing synchronous engine remains useful for probes
and isolated protocol tests.

Admission counts queued commands and undrained outcomes against a 32-entry budget. The result
queue has capacity 128 and coalesces outcomes for the same protocol/message,
preserving Delivered over stale Sent. It is independent of the ordinary event
bus. Incoming text/data/raw buffers hold 64 entries each and drop the oldest on
overflow. Configuration/security/identity changes invalidate the old receive
generation and cancel queued old-context work. An operation already dispatched
may finish and reports its original protocol. Metadata-only updates retain
received data and queued work; an ID broadcast may wait behind pending name
updates. Shared atomic ID allocation covers UI requests and engine control
packets so the two paths do not allocate independent counter ranges.

Application services consume protocol-tagged outcomes on the main thread and
update the message ledger before notifying UI consumers. Shutdown joins the
owner thread and drains remaining outcomes without calling a closing UI. Queue
acceptance is not physical-send success. A failed driver send records Failed,
not Sent. Result events also update the core ledger when the chat page is absent
or another protocol is active. The SQLite status reader now recognizes
Delivered; existing rows with that stored value need no migration and no longer
reload as Incoming. Broadcast NodeInfo now submits a real identity broadcast
instead of only changing the local contact record.

Coordinator metadata getters use a try-lock refresh while idle and return a
published cache during an in-flight operation; they do not wait for its transport
mutex. The enabled flag is atomic. Backend metadata methods must stay cached and
must not issue SPI/GPIO transactions; candidate checks inspect immutable endpoint
paths. Direct command operations still serialize through the transport lock.

Asynchronous key-verification failures carry an explicit runtime-failure flag
and protocol source to the existing terminal event. The UI maps that to Failed /
RuntimeUnavailable and clears an old comparison code. Tagged notifications from
an inactive protocol are ignored. Event enum values and normal ESP event behavior
are unchanged; the Pager build passed after the shared-field additions.

Blocking-driver tests hold acquisition/TX/RX while UI-side cache operations run,
fill the command queue, cancel 32 queued requests across retune, preserve an
in-flight old-protocol result, exercise failed transmission and restart, and
assert that driver I/O/release never runs on the UI test thread. A full ordinary
event bus does not lose typed outcomes. A receive/name-update test proves that
metadata changes do not discard an already-read packet. Injected-radio service
tests verify real broadcast submission, send-result persistence and terminal
shutdown state. These are software ownership tests, not peer RF verification.

The extracted armhf package also completed a QEMU mesh-mode startup using a
copied profile with the radio backend disabled. A fresh state root contained the
worker-created MeshCore identity record, a 1232 x 568 BMP was generated, and the
process exited zero. This exercises target-architecture worker/bootstrap/exit
without opening a real radio. The standard package passed Lintian via an isolated
Perl process after transient checker-internal failures in the host environment.

The migration covers the mesh adapter. Radio power toggles and foreground probe
entry/sampling still have synchronous call paths, and shutdown intentionally
waits for an in-flight bounded device call. Remaining GUI/device latency and
control cancellation/error presentation must be audited before completion.

## Walkie development

The native backend now uses the vendored Codec2 3200 codec (160 samples per
20 ms frame, eight encoded bytes) and the production WT v2 packet layout.
`modules/core_walkie` owns the shared header and bounded receive queue. ESP keeps
its existing service and uses the shared header writer. Linux's session owns
the foreground radio lease, exclusive audio focus, encoder/decoder and playout
state; the UI reads cached status. SDL capture starts only while PTT is held.
Release drains and pads a final partial packet; capture overruns or TX failures
stop the session rather than displaying simulated activity.

Both Linux radio backends expose configurable GFSK. The LR2021 backend switches
modems through RadioLib; the SX126x backend programs packet/modulation, sync and
CRC parameters directly. Leaving the foreground session restores the latest
mesh LoRa configuration. The SX126x image-calibration bands now cover 433 MHz
and its transport errors/BUSY deadline latch the device offline. No chip path
is selected by a shared-page device-name check.

Large touch layouts have a hold-to-talk button. The generic SDL host exposes a
consumable key observer; the application composition routes Space to PTT only
while the walkie page is active. Focus loss, window close, Alt+F4 and page exit
release PTT. Keyboard auto-repeat does not create another press. Compact ESP
physical-key routing remains in its platform adapter.

Tests use the real Codec2 implementation with fake radio/audio ports, checking
wire bytes, sequence wrap/gaps, queue limits, decoded positive/negative PCM,
tail padding, exclusive audio, failure cleanup and mesh restoration. A separate
native-service test checks asynchronous unavailable-radio behavior. These do
not establish microphone acoustics, LR2021 GFSK RF operation or peer audio
interoperability; peer-device validation remains deferred by the user.

## Multiple Linux devices

The generic desktop profile selects `runtime_mode=mesh|local|demo`, with an
explicit `TRAIL_MATE_RUNTIME_MODE` override taking precedence. CM0 ships `mesh`;
window title and facade title come from the profile. Device wiring belongs to
profiles and platform configuration rather than the shared UI.

Each production Linux state root gets one persistent node ID in its SQLite
database. Concurrent startup serializes allocation. The previous fixed ID is
retained only for explicit demo mode. `TRAIL_MATE_NODE_ID` accepts decimal or
`0x` notation and overrides the base/Meshtastic process ID without changing the
stored ID. MeshCore uses its own public-key-derived ID and does not use this override.
Existing settings, message history and key tables are preserved. Existing
installations previously using the shared fixed ID receive a new network ID
on first startup; peers may require rediscovery/re-pairing, or the administrator
can explicitly pin the old ID. Different devices must not pin the same ID.

## Remaining protocol integration

The Linux transport implements Meshtastic wire handling and now implements
MeshCore group text (type 5) and Trail Mate group application data (type 6)
through the shared MeshCore crypto/frame helpers. Both payload profiles are
decoded; configured send profile selects V1 or V2. Enabled channel slots and
public-channel fallback follow the production policy. UI-only pseudo-channels
are rejected. Plain group text is marked source-unverified because the channel
MAC does not identify its author. Receive queues and a 60-second signature cache
are bounded; signatures ignore route path length so relays do not create another
chat message. Sent signatures suppress our own relayed group text.

Independent fixed AES-ECB/HMAC-SHA256 samples, generated separately from the
production codec, verify exact encrypted V1/V2 text bytes. Adapter tests inject
a fake packet-radio port and exercise real send/receive, channel rejection,
wrong key/corruption, duplicate/self echo, TX disabled and protocol selection.
Malformed type/length combinations are checked without physical radio I/O.
The adapter no longer sends or accepts legacy TML1 frames as a fallback for a
selected production protocol. A protocol switch clears pending receive queues.

This comparison also found ESP's group-text sender writing an untyped zero path
descriptor for V2. It now calls the existing `encodePathDescriptor` helper, which
preserves V1's zero descriptor and encodes V2's empty two-byte-hash path as 0x40.
The Pager SX1262 build succeeded after this correction.

Linux MeshCore now has a persistent Ed25519 identity in its own SQLite table,
separate from Meshtastic X25519 settings. Seed/public-key creation is transactional;
concurrent initialization converges on one identity. A stored seed/public mismatch
fails closed without rotating the identity. Private expanded key material is
erased when its owner is destroyed. Cached identity getters do no storage work.
The active facade reports the correct protocol ID, while service initialization
always passes the base device ID to the underlying adapters so protocol switching
cannot overwrite Meshtastic's ID.

Send ID Local/Broadcast produce signed MeshCore advertisements in the selected
payload profile. Complete received advertisements are verified through the shared
protocol strategy before learning/persisting the peer key or publishing a contact
event. Unsigned key-only advertisements, changed keys under the same node ID,
older sender epochs and duplicate frames are rejected. Valid location fields are
projected into node-position events without inventing altitude or accuracy.
The initial adapter advertised only a chat role. The current client-forwarding
implementation selects chat/repeater advertisement and discovery roles from
the actual enabled setting; see [forwarding](linux-meshcore-repeater.md).
Signature validity does not mark a peer manually verified. Learned keys use a
separate bounded MeshCore namespace; a persistence failure rejects the update.

Tests cover independent Ed25519 signature bytes, signing/shared-secret symmetry,
identity creation races/restart/corruption, both advert profiles and route types,
signature tampering, key-only advert rejection, real adapter-to-contact events,
duplicate suppression, and application-service ID stability across repeated
protocol switches and a service restart. All RF tests use injected ports or a
disabled backend. Real peer/on-air verification remains deferred.

Scan Local now sends a MeshCore local discovery request with a cryptographically
random nonzero tag and collects matching full-key responses for five seconds.
The adapter returns Busy for a repeated scan or ID announcement during that
window, and group sends yield to receive collection. The scan is armed only
after successful radio transmission. Config application cancels the scan and
scheduled replies. Foreground radio ownership still takes precedence through
the shared coordinator.

The isolated `Discovery` state machine validates local route/empty path, exact
control lengths, tag, response window and node type. It deduplicates peers and
requests with bounded tables. Incoming requests honor type/since filters and
prefix-only requests. Responses use airtime-based randomized delays with at most
eight pending frames, one send per service tick and a five-second expiry. A
response waiting past its expiry is discarded. State uses wrap-safe elapsed
time; expired scans are retired so a later clock wrap cannot revive them.

Discovery controls are unsigned. They do not enter the trusted peer-key store
or set the public-key/verification flags used for private messaging. Unknown
peers get a provisional contact entry; known signed peers retain their names.
Signed advertisements remain the key-learning path. Tests verify exact request
bytes, response correlation/deduplication, wrong tags, expiry/clock wrap,
bounded scheduling, TX-disabled behavior, prefix/since handling, Scan Local
through the actual adapter, Busy gates and unsigned contact events. Peer/on-air
verification remains deferred.

Private MeshCore text (type 2), application data (type 7) and encrypted PATH ACK
(type 8) now use the persistent local identity and keys learned from signed
advertisements. The receiver checks destination/source hashes and authenticates
the cipher before delivering data. Public-key cache entries must match their
derived node ID on reload. Missing keys return PeerKeyMissing; discovery alone
does not authorize a private recipient. Sends start with flood fallback and learn
bounded, expiring paths from authenticated PATH acknowledgements matched to
pending messages. Radio/protocol changes clear those paths; see
[direct routing](linux-meshcore-direct-routing.md). Private app-data request/response semantics
beyond transport ACK remain incomplete.

The bounded direct state holds 32 pending acknowledgements and eight delayed
ACK replies. Text ACK signatures match the production SHA256 construction.
Encrypted ACKs must match both peer and token; standard short/multipart ACKs
are accepted only for a unique unexpired pending token. Legacy short ACKs are
protocol receipts, not authenticated peer identity evidence. Pending records
expire after 15 seconds; duplicate ACKs cannot deliver a message twice. A retry
of the same chat message replaces its old token. Replies are delayed at least
120 ms and expire instead of transmitting stale work. Protocol/radio-parameter
changes cancel pending messages and replies. A prompt ACK removes any queued
Sent result for the same message, preventing a later status downgrade.

The shared `trimPeerPlaintextPadding` helper preserves all four ACK token bytes
when the last bytes are zero. ESP and Linux use it, correcting the production
ESP parser's previous blanket zero trimming. The Pager build passed. Independent
Python Ed25519-to-X25519/AES/HMAC/SHA256 fixtures verify exact V1/V2 private wire
bytes; native codec/state/padding tests run with ASan/UBSan. Adapter tests exercise
bidirectional signed key exchange, private text/data, delayed encrypted ACK,
duplicate suppression, early ACK ordering, retune cancellation and key-cache
reload. No peer-device RF verification has been performed.

The retune regression exposed another old Linux mismatch: MeshCore modulation
was derived from the generic/Meshtastic BW/SF/CR fields. It now uses the MeshCore
frequency/BW/SF/CR fields, matching the production adapter. Tests assert actual
parameters delivered to the injected driver and ACK cancellation on a real
parameter change, rather than changing an unused config field.

MeshCore NodeInfo query/reply now uses the production control codec and shared
runtime, with fixed-control padding preserved on Linux and ESP. MQTT and client
forwarding are implemented and verified as described above. MeshCore active-Trace's existing
external entry is the phone facade, with no shared P4/compact UI caller; that
phone-facing port and Bluetooth identity APIs remain under the user's Bluetooth
deferral. MQTT is visible in the P4 settings and independent of Bluetooth, so
its Linux broker/business path is required. The working source now binds MT/MC
MQTT to the actual application worker and shared chat handler. Separate service
processes passed broadcast, key learning, encrypted private chat and persistent
status/inbox/provenance checks with physical radio operations forbidden. A native
CCM tag-length defect was reproduced and fixed with independent vectors. VMP
MQTT now has separate authenticated inbox and recorded-media publication owners.
The native targeted gate passed 4/4, including two voice/mesh-worker processes
with physical radio operations forbidden. Independent Mosquitto 2.0.18 runs
also passed MT chat, MC chat and VMP broadcast/private voice. Target/release
gates subsequently passed as detailed in the current package evidence above;
these MQTT changes are included in the installed `2d6836dd...` deb. See
[mesh MQTT integration](linux-mesh-mqtt.md).
Earlier raw-adapter Reticulum limitations are historical:
the current composed application has the proper LXMF/Reticulum transport,
including the dedicated LoRa ingress/egress path, as documented above. Capability
flags must still not claim missing MeshCore operations. The existing GD
application envelope has no explicit payload length
and receivers trim zero padding. New sends ending in a zero byte are rejected
rather than silently altered; a compatible length-aware business envelope is
still required for unrestricted binary payloads. No peer-device RF test has
been performed, as requested by the user.

## Radio dependency decision

### LR2021 SDR investigation (2026-10-29)

[LR2021-SDR](https://github.com/SAMS0N1TE/lr2021-sdr) is a candidate for an
additional raw-IQ receiver, not a replacement for the current packet receiver.
Its MIT license requires retaining the copyright and license notices when code
is incorporated. Its published measurements use an LR2021 ROM 1.24 on a
T-Display P4 LR2021 board; they do not establish CM0 throughput or RF coverage.
The current CM0 profile selects LR2021 over Linux spidev at 1 MHz. The P4
follow-up now has explicit TFT/AMOLED LR2021 packet targets and a native IDF
backend alongside SX1262. The user confirmed TFT + LR2021, and that exact
firmware target compiles and links. Physical RF, raw-IQ and FM acceptance remain
separate; see the [P4 follow-up](t-display-p4-hardware-audit.md#lr2021-variant-follow-up-2026-10-29).

At 1 MHz, SPI carries at most 125,000 bytes/s before framing and scheduling
overhead. The upstream 125,000-sample/s IQ16 mode requires 500,000 bytes/s
(two 16-bit components per complex sample). The current CM0 bus configuration
therefore cannot sustain even that mode. A higher bus clock, bounded BUSY
handling and measured loss-free FIFO service are prerequisites, not an assumed
consequence of enabling the decoder.

For Protocol Probe, measure candidate configuration time, actual receive dwell,
packet queue drops and UI latency separately. The Linux LR2021 `configureLoRa`
now uses the shared `LoRaReceiveConfig` transaction to omit unchanged setters.
It invalidates the cache on reacquisition, final release, FSK configuration and
failed I/O, and publishes applied settings only after receive restart succeeds.
Even identical candidates still enter standby and restart receive. The focused
transaction test uses the production configuration type and transaction code:
full configuration makes 12 driver calls, an identical request makes two, an
SF-only change makes three, and a frequency-only retune makes four. Retuning
also reapplies output power because RadioLib selects the LF/HF PA table using
the current frequency. These are driver-call counts rather than
SPI transaction counts or measured RF throughput. Every full-configuration
failure position, partial delta failure and transport-health rejection is
followed by a checked full recovery in the test.
No reduction in SPI delays or increase in bus clock is justified by the
upstream P4 measurements alone. Raw-IQ support should be a separately leased
radio mode with bounded queues, loss counters and a generation boundary on
retune; stopping it must restore the packet modem and normal receiver owner.
See the upstream [porting requirements](https://github.com/SAMS0N1TE/lr2021-sdr/blob/main/docs/porting.md).

The Linux packet-radio port now has an optional finite `captureIq` contract.
Support defaults to false for backends without an implementation. The Linux
LR2021 driver now implements finite capture through the pinned portable RX
engine; SX126x and unavailable backends retain the unsupported default. This
driver integration is now connected to the conditional Protocol Probe IQ entry;
physical IQ acceptance remains pending. The coordinator permits
capture only through an acquired foreground lease and holds its operation lock
through capture and recovery. It rejects overlapping/undersized buffers,
unsupported backends, incomplete results, capture errors and failed/offline
recovery; rejected results publish zero bytes and no nominal rate. A soft
capture failure may leave an otherwise restored radio usable. Failed recovery
invalidates the coordinated configuration, blocking subsequent packet I/O until
checked recovery. Foreground release retains the existing checked mesh restore.

The coordinator regression tests cover lease/power/capability admission, buffer
validation, valid and partial results, sticky backend-online flags after failed
recovery, explicit configuration recovery and deferred mesh configuration.
A blocking fake capture also verifies serialized physical operations alongside
nonblocking cached status queries, followed by mesh resumption after release.
The actual Linux application and affected callers rebuilt successfully; the
coordinator and Walkie session regressions passed. These are host contracts,
not evidence for physical capture or measured throughput.

The Linux LR2021 adapter uses the coordinator-owned transport under its driver
mutex, reads the actual ROM/configuration and requires one acquired LoRa owner.
The engine captures 256 pairs into caller-owned storage, backs up and verifies
SRAM around reset, and reapplies the saved LoRa configuration and receive state.
Reset retains the configured TCXO voltage; failed recovery holds the configured
reset GPIO and takes the driver offline. Error codes are statically checked
against the pinned engine's domain. Unknown nominal rate remains zero.
The capture hook clock uses the process monotonic clock across HAL reset.

Production adapter methods are extracted into a host test covering SPI framing,
big-endian addresses/data, command-width restoration after failure, actual ROM
reporting, exclusive LoRa preflight, TCXO/reset and configuration restore errors.
The application rebuilt/relinked with both `Lr2021Radio::captureIq` and the
portable `lr2021_iq_capture` present in its final symbols. Driver/coordinator/
configuration regressions passed. Physical ROM/capture/recovery acceptance remains pending. Finite SRAM
capture is distinct from streaming and does not relax the 1 MHz throughput limit.

The Linux diagnostic worker now connects the Probe button/`I` shortcut through
the foreground coordinator to this driver. It is unavailable in demo/synthetic
LoRa mode and requires a supporting backend with existing SPI/GPIO endpoints.
The page stops its receive session first; a worker-side release barrier waits
for in-flight I/O and mesh restoration to finish rather than trusting the
immediate UI Idle state. A replacement active session rejects the handoff.
The receiver marks hardware busy under its mutex before every new I/O, including
requests arriving during an older release. Immediate request/release and that
replacement-session interleaving are covered by the handoff regression.

Only checked capture plus successful foreground release produces Ready. Missing
hardware, acquisition failures, partial data and failed recovery publish no
samples. The worker owns its IQ buffers and never touches LVGL objects; page exit
joins it before another page can acquire the radio. Small-screen results use a
four-line summary and a separate scrollable viewport for long errors. The Help
map documents IQ and scrolling. [Marked UI fixtures](../images/cm0/iq-review/README.md)
show both layouts using test data and do not prove physical reception.

The Linux packet receiver also rejects CRC, LoRa header CRC, length and address
errors before reading any payload, including when `RX_DONE` is set. RadioLib's
`readData` does not itself reject length/address IRQs. These errors update the
matching receive counters and restart RX; progress/FIFO/TX-only flags do not
restart an in-progress receive. Hardware, command and PA protection faults take
precedence over packet completion: the driver attempts standby, holds the reset
GPIO when configured, marks the radio offline and invalidates its configuration
cache. It never publishes a packet from that interrupt state.

`linux_lr2021_receive` extracts the production receive method and exercises it
with the pinned RadioLib constants and fake radio/GPIO boundaries. It verifies
damaged-frame rejection with and without `RX_DONE`, failure recovery, fatal
reset/offline behavior, progress preservation and valid-packet delivery. The
driver and actual Linux application rebuilt/relinked successfully, and the
receive/configuration/IRQ regression tests passed. This is host evidence, not
physical RF acceptance; the CM0 SPI clock remains 1 MHz.

A broadcast-FM receiver requires a real chain from IQ acquisition through
channel filtering, wideband FM demodulation, de-emphasis and resampling to the
existing audio output service. It must release ownership when another radio
tool starts. The upstream advertised 100 MHz lower bound does not prove full
87.5–108 MHz coverage on this board. Its higher sample-rate modes are candidates
for wideband FM; narrowband voice demodulation alone is insufficient evidence.
The currently pinned RadioLib packet API accepts LF frequencies only from
150 MHz, with parameter checking enabled in our native build. A future FM
receiver needs its own exclusively owned receive-only tuning lifecycle; widening
packet TX/RX validation is not part of this investigation. Raw sources 0/1
provide signed IQ8, while source 6 carries gain-tagged IQ6 and cannot share that
decoder. The FM consumer now explicitly accepts IQ8 and IQ16, resets across
format changes and rejects tagged/unknown formats. Host tests verify identical
PCM for equivalent IQ8/IQ16 sequences and a low-resolution full-deviation FM
tone. No streaming, RF passband or physical FM acceptance is implied.
[LakeShark](https://github.com/SAMS0N1TE/LakeShark) has related decoder and audio
integration, but its detailed [FM guide](https://github.com/SAMS0N1TE/LakeShark/blob/caf3fbe9e28103d14baa5639f37dc43d307a8149/docs/apps/FM.md)
still describes WFM using an external RTL-SDR or HackRF. The pinned
[`fm_lr_dsp.h`](https://github.com/SAMS0N1TE/LakeShark/blob/caf3fbe9e28103d14baa5639f37dc43d307a8149/components/lakeshark/apps/fm/fm_lr_dsp.h)
resolves the mode distinction: its LR IQ path accepts narrowband FM, AM and
several data decoders, but excludes `FM_MODE_WFM`. It consumes 125 ksample/s IQ
and imposes a 104 MHz lower tuning limit. Thus the current analog FM support
is not evidence of a ready-to-port broadcast FM receiver. No FM launcher entry
or completed FM capability is introduced by this investigation.

LakeShark carries a [GPLv3 license](https://github.com/SAMS0N1TE/LakeShark/blob/caf3fbe9e28103d14baa5639f37dc43d307a8149/LICENSE),
unlike the MIT radio-access library. Do not treat the two repositories as one
interchangeable dependency or copy decoder sources under the radio library's
license. This investigation has incorporated no upstream source code.

Arduino ESP builds retain their existing RadioLib 7.4.0 dependency. The native
IDF LR2021 and Linux targets use an isolated shared dependency declaration.
The [upstream 7.8.1 release](https://github.com/jgromes/RadioLib/releases/tag/7.8.1)
contains LR2021 fixes. Its verified tag resolves to
`2325a38a89dd0f0e402c19f67fe910111da1e246`. Linux SPI/GPIO and IDF SPI/expander
HALs remain platform-owned; packet configuration transactions are shared. The vendor
sample has a modified older RadioLib/CM0 HAL with hardcoded global descriptors;
do not copy that ownership model into shared runtime code.

## Onboard 1seg radio integration, 29 October 2026

The CM0 profile now explicitly selects `radio_rf_switch_profile=cm0_16e8`.
Its DIO7/DIO6 function and mode writes are checked, selected only for that board
profile, and reapplied after cold start, GFSK initialization, LoRa restoration
and IQ reset before RX resumes. Generic Linux radio configurations default to
`none`; unprofiled LR2021 backends do not advertise the optional TV IQ port.
Native tests cover all four RF-switch write failures in each lifecycle path.
They establish software behavior, not RF sensitivity or programme reception.

The current continuous IQ port and separate television integration gates are
tracked in [CM0 1seg TV](t-display-cm0-oneseg-tv.md). It uses the pinned MIT RX
engine with checked queue publication and cancellation; no transmitter or MMIO
backend is added. The TV worker, decoder, media output, menu/page and actual
on-board reception acceptance remain pending. Earlier deployed-package evidence
in this ledger does not imply that these new changes have been deployed.

## Completion gates

A subsequent source audit reproduced a foreground receive-state defect using
an optimistic fake radio: an acquired but unconfigured port could sample RSSI,
and failed retunes retained the old configured flag. The Linux UI LoRa port now
publishes its frequency/configuration only after backend success, invalidates
receive admission on a rejected retune, and returns NaN without hardware I/O
while unconfigured. A focused test verifies initial admission, failed and invalid
retunes, rejection of stale RX/TX, recovery and release. It failed before the fix
and passes afterward. This correction is included in the current `1595c016...`
deb; the preceding 203-test release remains full-regression baseline evidence.
The subsequent source integration moves shared Protocol Probe acquisition,
retuning, TX and RX to a generation-tagged Linux receive worker. Dwell starts
after configuration readiness and response windows start after TX completion.
The non-Linux compatibility path retains its existing synchronous port and
creates no new task. See [foreground receive sessions](linux-lora-receive-session.md).
This integration is also included in the current installed package.

Quick-control radio requests now share the mesh worker's hardware owner. The UI
reads a separately synchronized mailbox and displays the actual software-enable
state alongside pending/failure status. A foreground lease can refuse disabling;
that failure preserves the enabled state and saved preference. Only successful,
current requests write the preference, on the worker. A write/readback mismatch
reports failure while retaining the actual radio state. This software permission
does not imply physical supply switching on a board without that capability.
The registry holds a weak mailbox owner, so stopping application services makes
further requests unavailable without retaining a dead worker pointer. Foreground
tool operations in the shared Protocol Probe page now use that source receive
worker. Standalone GTK RF tools retain synchronous calls in their own composition.

- Map every fitted capability to a real adapter and applicable UI/business flow.
- Distinguish absent, present-but-failing and functional hardware in diagnostics.
- Complete software protocol paths and shared-radio ownership tests. Peer-device
  interoperability tests are explicitly deferred by the user.
- Complete applicable audio/brightness/network/storage integration and validation.
  Physical GNSS/IMU validation is excluded for this baseline because neither
  expansion is fitted; Bluetooth and AHT20 remain excluded by user request.
- Exercise real fullscreen, touch navigation, keyboard editing and Alt+F4.
- Rebuild the standard armhf deb, lint it, install/test the exact package on CM0.
- Audit GitNexus impact/changes plus platform boundaries and appropriate tests.
- Audit invalid radio-configuration readiness and UI-thread hardware I/O latency.
- Mark complete only when every required item has direct evidence.
