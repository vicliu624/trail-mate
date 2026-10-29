# CM0 adaptation acceptance audit

Audit date: 23 October 2026. This separates current evidence from the historical
[integration ledger](t-display-cm0-hardware-integration.md). The original CM0
adaptation and Cardputer Zero maintenance scope is accepted. This does not claim
exhaustive physical testing or completion of every formal management operation.

## Delivered artifact

Scope control: the CM0 adaptation and Cardputer Zero maintenance remain the
delivery requirements. Additional feature work is permitted only after an
explicit assessment shows that moving duplicated logic into a shared module
will let ESP32, nRF and Linux reuse it. Completing every web management operation
is not an adaptation acceptance requirement. Existing extra work is retained;
it must not be described as delivered merely because it exists in the worktree.

The local and installed standard package is `trailmate-cm0_1.0.0_armhf.deb`:
SHA-256 `ecd610b457901f59e15c96e4d9912ff4ed6426176413fa22c678222dc3e4f149`.
Installed `/usr/bin/trailmate-linux`:
`e2f330fe6443af729c6391ad218206a5f72885c24efdbe576323bbcf4163061c`.
The original logo remains
`dc0872a677e634173c8edf62c777b900804f9eeb835c19fb714ee64f855a4143`.
These hashes and `dpkg -V` were rechecked on the actual CM0 during this audit.
Isolated installed startup/timed exit and Lintian passed for the current package.
A superseded cleanup candidate had an inconclusive Lintian result because the
host Perl process crashed/failed loading its profile.
The pre-creator package is retained as `trailmate-cm0-before-tmap.deb` on CM0
and `.tmp/cm0-deb-before-tmap.deb` locally. Its SHA-256 is
`0a2440d9d75fccbf800fd6f7b27ceb6014f9cf75909bb7c4c0ad469bb4a8ab70`.

## Requirement and evidence mapping

### TMAP creator follow-up

The user explicitly requested on-device map creation after adaptation acceptance.
CM0 and Cardputer Zero now share a seven-page Extensions wizard and Linux worker;
the writer/importer/rendering format is reused from Center's existing `TmapTool`.
See [on-device TMAP creation](linux-tmap-creation.md) for operation and packaging.
The current CM0 package above includes the self-contained linux-arm producer.
The initial release's Lintian returned success with narrowly documented
vendor-library overrides; the host Perl/readelf helper emitted warnings for
three runtime assets. Later cleanup verification is recorded separately below.

The complete native release gate passed 335/335 and the compact suite 19/19.
After the final source-refresh and Extensions-return fixes, the seven creator
tests passed again, both Cardputer Wayland/fbdev targets built, and the three
compact composition/packaging checks passed. On the actual CM0, all four task
contracts and both UI layouts passed. The installed real producer generated a
TMAP from a small genuine OSM PBF; the independent TMAP SDK read its raster tile
and normal map search found its POI. These fixtures used isolated temporary
storage, not user maps. Installed application startup/exit, original-logo hash
and `dpkg -V` also passed. This is not a large-region performance benchmark or
physical Cardputer test; terrain/satellite network sources were not exercised
by the OSM fixture.

Cardputer's linux-arm64 producer was published, and both app variants and compact
composition were validated natively. Its ARM64 deb was not generated: Docker
Desktop failed at its inference runtime socket. The user explicitly deferred
Docker repair and requested CM0 package plus both source implementations.
Existing unfinished Geocaching management operations remain behind
`TRAIL_MATE_EXPERIMENTAL_GEOCACHING_DRAFTS=1`; this package does not enable them
by default.

The cleanup follow-up moves downloaded source rasters into the job workspace
instead of retaining `cache/tmap-sources`, and excludes Center's map-authoring
JSON reports from installed map payloads. The five backend cases passed natively
and on CM0, including real production/read/search. They assert an empty job-cache
directory, preserved PBF and only `.tmap` files beneath the output map directory.
Two UI layouts passed on CM0 as well. The final candidate's archive metadata,
architecture and required payloads passed `dpkg-deb` checks. Lintian on this
candidate could not be completed: two host Perl runs exited with SIGSEGV and a
third failed loading Lintian's profile. No Lintian pass is claimed for it, and
the failure was not used to trigger further compiler or CPack runs.
That intermediate cleanup package was installed on CM0 with clean `dpkg -V`,
original-logo verification and isolated startup/timed exit. It has since been
superseded by the recovery/keyboard package identified above.

### Interrupted-job recovery and Linux keyboard follow-up

The user requested automatic next-start recovery and Pager-style keyboard
shortcuts on both Linux products. All producer files and publication staging
now reside in marked, private `trailmate/.tmap-jobs/job-XXXXXX` directories beneath
the configured storage root. Recovery uses descriptor-relative traversal and an
inherited process lease; it never traverses input or final-map directories for
deletion. Live orphan producers are protected, with retry after they exit.
Unknown sibling files and unmarked legacy paths are preserved. The tiny ownership
record remains as control metadata, not a raster cache.

The existing Pager input adapter now serves Linux too, including contextual H
help and W/S navigation on applicable pages, while retaining literal text and
native page semantics. Both SDL and Cardputer's Wayland/framebuffer LVGL bridge
call it. Map accepts H and F1. Extensions exposes Make TMAP directly in its bottom
bar. Background refresh cannot steal help/menu focus, and the creator resets
keypad state before replacing an editor on Esc. See
[Linux keyboard shortcuts](linux-keyboard-shortcuts.md).

The focused native gate passed 7/7, including real producer/SDK/search, SIGKILL
restart with a live orphan, private staging/symlink cleanup, PBF/final/unknown-file
preservation and real SDL key press/release events in both layouts. Both Cardputer
targets built and its three composition checks passed. On CM0, four backend
contracts (including the SIGKILL/restart case), both keyboard/UI layouts and real
production/read/search passed. No actual power cut was performed. The current
standard deb passed Lintian and was installed with matching hashes, clean
`dpkg -V`, original-logo verification and installed startup/timed exit. The running
previous app was closed through the compositor only after checking for an active
producer. No kernel, boot, Bluetooth or display-driver setting was changed.
The exact installed application was then opened on CM0's actual fullscreen
Wayland desktop with isolated data roots and radio disabled. Keyboard navigation
reached Extensions, H was opened/closed, and one screenshot confirmed the visible
Make TMAP bottom-bar button and H Help top-bar hint. This is additional evidence
of the installed entry, rather than a standalone-page render.
After verification, the normal installed device profile was relaunched and left
open on Extensions. Its hardware-dependent catalog placed SSTV and Extensions at
indices 12 and 13, so the restore used the catalog log rather than assuming the
isolated profile's launcher index. The installed executable hash and `dpkg -V`
were checked again after restoring that session.

The subsequent Extensions M-key correction binds M/m in that page's existing
key handler, consuming the event before toggling its shared menu. The regression
uses real SDL M press/release to open and close it, plus uppercase M and Esc,
in both layouts. The seven focused cases and compact composition checks passed;
both Cardputer targets and CM0 rebuilt. The two keyboard/UI cases also passed on
CM0. The updated deb passed Lintian, installation, original-logo and installed
startup/exit checks, with the current hashes above and clean `dpkg -V`.

Make TMAP additionally has a direct T/t function shortcut in Extensions, including
when its menu is open. It dispatches the same button action as touch/Enter; the
button reads T Make TMAP and contextual Help advertises T. The updated native
7/7 gate and CM0's two UI cases cover lowercase T, uppercase T from the menu,
and literal t in a focused editor. Both Cardputer targets built. The current
deb passed Lintian and installed-file/startup checks; no extra screenshot was
taken for this shortcut follow-up.

| Requirement | Current evidence | Acceptance boundary |
| --- | --- | --- |
| Reuse P4 UI | Installed profile selects `t_display_p4_touch`, 1232 x 568, rotation 0, fullscreen. Earlier real Labwc sessions verified fullscreen/focus. The user confirmed normal touchscreen operation, keyboard navigation and Alt+F4 exit on 23 October 2026. | Normal physical interaction is accepted. This does not establish exhaustive multi-touch or individual key-matrix coverage. |
| Linux device separation | CM0 facts reside in `boards/t_display_cm0/device.conf`; Cardputer facts reside in its board composition. Common runtime uses explicit selectors or rejects ambiguous input. | No generic runtime should infer a board from a device-node index, interface name or PWM class index. |
| Power and battery | Fresh actual probe: battery available, 99%, charging false, external power true. Cached workers separate charge state and external power. | Unplug/replug and active-charging transitions have not been observed. |
| RTC | Fresh probe: available, valid, system-clock source true; packaged RTC unit active. Earlier authorized reboot verified kernel hctosys. | Coin-cell retention and alarm wake have not been demonstrated. |
| Keyboard | Fresh probe detects the keyboard; evdev/SDL input and text commit/preedit paths are implemented. Earlier installed form entry, persistence and compose input passed. Keyboard-light worker/menu and actual PWM readback passed. The user confirmed normal keyboard navigation and Alt+F4 exit. | Normal keyboard navigation is accepted; no exhaustive individual key-matrix claim is made. |
| Wi-Fi and IP network | Fresh probe: Wi-Fi supported/connected and generic network supported/connected. Actual scan, system-routed HTTP/HTTPS and worker fixtures have evidence. | Actual AP switching, power cycling and wired cable/link operation remain unverified. |
| LoRa | Actual LR2021 ROM reads and reset/SPI checks passed. Configurable driver, mesh/foreground ownership, asynchronous RF tools and protocol business paths are implemented. | Peer/on-air verification is expressly deferred by the user. |
| Chat and protocols | MT/MC MQTT, private chat, NodeInfo, forwarding and Reticulum/LXMF have focused source/native/ARM evidence. Actual MQTT application pairs and VMP worker pairs passed on CM0. Independent Mosquitto interoperability passed on the host. | Do not infer physical RF interoperability or broker TLS coverage from loopback tests. |
| Audio features | Native ALSA/SDL transport, shared music/focus, Codec2, VMP, walkie, SSTV and native call composition have targeted evidence. VMP recorded-media tests use synthetic PCM. | No external headphones/speaker are attached. Acoustic microphone performance and external-peer calls remain unverified; codec feedback does not prove a microphone is fitted. |
| Maps, tracker and extensions | Offline navigation/search, packaged map rendering, recording storage, signed route images, remote catalog/download/install and persistence have dedicated native/ARM and installed-UI evidence. | Physical removable-media transitions and worst-case package/UI latency are not established. |
| Display dimming | Fresh system inventory has no backlight class device. Optional Wayland gamma adapter and isolated compositor fixtures pass; the bounded actual compositor probe accepted a ramp and control release. The user observed no noticeable dimming and reported a normal picture. | Optical dimming effectiveness is unconfirmed. `screen_power=none` remains selected; no further display experiments are planned. Experimental panel modules must not be deployed. |
| USB device mode | Fresh `/sys/class/udc` inventory is empty. Existing HostLink TCP and application management paths are separate from USB device mode. | Do not claim a configured USB gadget/mass-storage transport from the presence of a USB host hub. |
| Cardputer Zero maintenance | Compact UI retained. Calculator, Geocaching, Protocol Probe and Agenda are registered in the current 14-route manifest. Both Wayland/fbdev targets rebuilt; latest compact suite passed 19/19. Existing app-shell, business-wiring and packaging checks were rerun without rebuilding and passed 3/3 during closeout. | User has no device. Host tests do not prove physical session/input/GNSS/LoRa behavior. The checked-in screenshot set covers ten earlier pages, not the whole current 14-route manifest. |
| Standard deb and logo | Current local/device hashes agree; lintian, installation, file integrity, original logo and installed startup/exit checks passed. | This establishes packaging and startup, not complete hardware acceptance. |

The pre-creator artifact's native complete run passed 317/317 in one run. The compact suite
passed 19/19. Earlier targeted NodeInfo and production MQTT/Mosquitto
evidence remains valid. Identity restore additionally passed native/browser
lifecycle fixtures, five ARM groups, independent ARM TCP/exec/signature proof,
and the exact installed application's isolated exec/re-proof path. ESP stack
hygiene and Reticulum scope guards pass. The latest whole-worktree GitNexus
audit is CRITICAL: 217 files, 849 symbols, 28 flows, including accumulated
changes. New native symbols are not fully indexed, so that audit does not prove
their complete impact coverage. No commit was made.

## Explicit scope decisions

The final read-only CM0 probe rechecked the installed executable, standard deb
and original-logo hashes above, with a clean `dpkg -V`. RTC availability, validity
and system-clock source were true; its service was active. Battery level was 99,
external power was true, charging was false, and keyboard/Wi-Fi/network were
available. The installed profile still selected the P4 UX, 1232 x 568, rotation
zero, fullscreen and `screen_power=none`. No USB device controller or backlight
class node was found. This check changed no driver, boot or display setting and
started no compilation or screenshot capture.

On 23 October 2026 the user reported: touchscreen and keyboard navigation are
normal, and Alt+F4 exits successfully. This closes the outstanding normal-input
acceptance gate. Charging transitions, AP/cable switching, removable media and
acoustic audio remain without exhaustive physical acceptance evidence; their
implementation/evidence boundaries are stated above. They are not new mandatory
test rounds added to the agreed adaptation scope. Explicit exclusions and
deferred RF/Cardputer tests remain as recorded below.

## Closeout

The final requirement audit covers the P4-based CM0 UI, fullscreen and exit,
board-profile/runtime separation, fitted peripheral integration, Cardputer Zero
maintenance, and the standard armhf deb with the existing logo. Evidence is mapped
above. The user-confirmed input result completes the remaining agreed acceptance
gate. The package SHA-256 was rechecked locally at closeout and matches the
installed artifact recorded above. No additional build, deployment, display
change or scope extension was performed.

The Geocaching 64–66 work below is intentionally retained but unreleased. Its
future completion is separate from this adaptation delivery. No commit was made.

- Bluetooth/phone protocol support is disabled and deferred by the user.
- AHT20 collection is excluded by the user, despite the sensor being fitted.
- NRF9151/GNSS and QMI8658 expansions are not fitted to this CM0 baseline.
- No camera was enumerated. Generic optional backends do not establish fitted hardware.
- No external audio transducer is connected. Vendor documentation describes
  headphones and an expansion speaker, not evidence of an attached acoustic path.
- Physical Cardputer Zero validation and LoRa peer testing cannot be inferred
  from the host/loopback tests; the user has no Cardputer device and deferred RF tests.

## Additional management work outside adaptation closure

Unreleased Geocaching operations 64–66 remain in the worktree at the user's
request. They add draft catalog/read/save, a kind-2 upload owner, atomic save
receipts and native UI cache invalidation. The focused run passed 17/18; the
remaining browser test needed bounded retries for the existing asynchronous
configuration-upload Busy response and passed its single-test rerun. Thus all
18 focused checks have passing evidence. No additional product compilation was
needed for that test correction. These changes have not received a full native,
compact or ARM release gate and are not in the installed deb above. Further
Geocaching publication/signature/confirmation operations are deferred. Published
draft retention remains incomplete; the implementation explicitly refuses that
unsupported path instead of fabricating publication metadata.

HostLink author proof, uploads, inventory/cleanup, signed package installation,
current-schema settings snapshots/import and restart receipts have native owners
and evidence. Historical notes saying these are absent are superseded.
The formal browser client also exposes additional author/radio-identity and
owned-track management operations. Linux does not implement all of those
operations and advertises its actual subset. Review these contracts and their
applicable target scope before claiming complete software parity. These remaining
management gaps do not expand the hardware-adaptation delivery requirements.

Publication operation 83 and track job operations 48/50 are included in the
current package. They use the mesh worker, pinned verified directories,
generation-bound durable admission, V1/V2 content upload and signed publication
receipts. Historical lookup and pre-publish cancellation survive worker restart.
The focused integration gate passed 37/37, and all 28 new publication scenarios
passed on CM0. Installed startup and the existing exact-application directory
wire check passed after installation. Publication responder tests establish the
contract with real signatures and journals, rather than independent server or
on-air interoperability. Further owned-track
management parity remains under review. See
[native track publication](linux-hostlink-track-publication.md).

IP publication confirmation (97) and history import (98) are also included in
the current package. The focused integration gate passed 42/42, including the
production web client over TCP. Ten native confirmation cases passed on CM0;
an independent Python/OpenSSL client passed confirmation and import against both
the ARM fixture and the exact installed main application. Reconnect, durable
receipt persistence and explicit proof-upload cleanup were checked with temporary
identities and data. Operation 84 is reserved in the current formal implementation,
not an implemented device history-list contract. See
[IP publication confirmation](linux-hostlink-track-confirmation.md).

Remote track reads 91–94 are included in the current package. All nine formal
network read categories (0, 1, 2, 8 and 9–13), session ownership, wrong-source
and forged-signature rejection, disconnect, protocol-context retirement and
timeout passed the fourteen new native scenarios. The focused integration gate
passed 55/55, including the production browser's TCP read adapter. All fourteen
scenarios and independent TCP/OpenSSL manifest/file/digest/cleanup checks passed
on CM0. The exact installed main application advertised the operations and
rejected unpinned directories, stale identity and invalid or expired reads.
Positive read-response tests use a pinned directory contract responder; live
independent-directory and RF interoperability remain unestablished. See
[remote track reads](linux-hostlink-track-read.md).

Formal identity backup export and restore are now wired to the HostLink worker,
application quiescence and restart owner. Seed and expanded formats, generation
advance, rollback, concurrent-owner exclusion, lost response and fresh proof
passed their respective fixtures. These operations are included in the current
installed package above; restore is advertised only with an active matching
frontend/lifecycle owner.
See [identity backup and replacement](linux-hostlink-identity-backup.md).

Wireless identity operations 7-9 now bind to both Meshtastic/MeshCore backends,
the shared frontend lifecycle bridge and atomic author/radio compare-and-swap.
The installed package includes atomic local key-pair save/clear/snapshot reads,
lifetime cache leases and refusal to regenerate corrupt or unavailable keys.
The focused gate passed 12/12. Actual CM0 fixtures passed both radio kinds,
same-key generation advance, busy/stale rejection, transaction rollback, lost
response, real exec and fresh author proof. Four atomic-store scenarios and the
author seed/expanded/write-failure regressions also passed on ARM. No current
user identity was exported or replaced. See [local identity storage](linux-local-identity-storage.md)
and [wireless identity storage](linux-radio-identity-storage.md).

Old engineering notes are chronological evidence, not a current task list.
For example, remote extension delivery and NodeInfo query/reply are implemented;
older paragraphs describing them as missing must not trigger duplicate work.

The current package includes track catalog operation 80 and snapshot operations
81/86-88 through separate worker-owned coordinators and the configured storage
root. Formal clients cover 204 catalog records and four-format snapshot imports,
including reconnect, source mutation after sealing, hash checking and release.
The complete native run passed 246/246. A final post-lock directory admission
guard additionally passed the affected 21/21 checks, including exact-boundary
lock interposition; only affected targets were rebuilt after that guard. Same-size
rewrite rejection passed 100 consecutive native runs before the guard. Nine
snapshot scenarios and independent TCP/catalog/hash/reconnect checks passed on
the actual CM0. The exact installed executable additionally passed the isolated
four-format wire chain with reconnect, post-seal source mutation, SHA-256 and
idempotent release. Publication and other remaining management contracts
remain open; see [Linux track management](linux-hostlink-track-management.md).

Local track preparation/signing operations 82/96/99 are now included. Shared
candidate, geometry, attachment, reservation, signature and head rules compose
with native file/identity leases and immutable content retention. The complete
native gate passed 250/250, including an injected reservation write failure.
The formal client covers V1, V2 assets, V2 without assets, exact signature
verification, reconnect recovery and conflicting/stale requests. CM0 passed
native preparation/recovery/write-failure and signed-input regressions, plus
independent TCP/OpenSSL checks for all three forms. No user identity/track was
changed. The exact installed formal executable passed those three signing and
reconnect paths using isolated dummy-SDL profiles. See
[local track signing](linux-hostlink-track-signing.md).

Directory list/discovery 89/90 now bind to a database-scoped, session-owned
metadata seam and the native mesh-worker directory pool. Signature-verified
announcements and nonce-bound capabilities feed that pool through the existing
Reticulum transports. Custom application traffic uses the original encrypted
link/resource/proof machinery and a separate outcome sink from chat. The focused
integration gate passed 26/26, complete native regression 263/263 and compact
regression 19/19. Actual CM0 directory state, announcement authentication, nine
custom delivery cases, real TCP routes and independent session checks passed.
The exact installed executable passed the directory session checks in isolated
mesh mode with physical radio disabled and dummy SDL. The original icon and
installed-file integrity were rechecked.
Publication/history remain open. See [directory discovery](linux-hostlink-track-directories.md).
