# Cardputer Zero production parity review

This review compares the Cardputer Zero composition with the shared production
catalog and Linux services. No Cardputer Zero device is available. Compilation,
host tests, and rendered screenshots are software evidence; they do not establish
physical keyboard, radio, GNSS, or session integration readiness.

The requested scope is a production-parity audit, not a reported device defect.

MeshCore client forwarding is now connected through the existing mesh worker,
including policy limits, duplicate/echo suppression, directed/ACK/Trace relay
and enabled advertisement/discovery roles. Eight native and assertion-enabled
CM0 ARM fixtures passed; the wire planner also passed ASan/UBSan. Both compact
device targets rebuilt, with no new Cardputer wiring assumptions or physical
acceptance claim. See [MeshCore forwarding](linux-meshcore-repeater.md).

The latest shared Linux brightness/network release rebuilt all compact targets
and passed all 19 compact checks in one run. Optional Wayland gamma control is
selected through generic device configuration and is not enabled by default.
Wireless selection refuses multiple adapters without an explicit interface name;
saved-profile cycling runs on the native worker, preserves desktop connections,
and cancels on manual operations or interface changes. These changes introduce
no CM0 hardware identifiers into the compact composition. Host and CM0 ARM
fixtures cover the shared adapters; no Cardputer Zero hardware was available.

The subsequent shared Protocol Probe update uses a generation-tagged Linux
receive worker for acquisition, configuration, RX and verification TX. The
compact Wayland and framebuffer device targets rebuilt successfully. Native and
CM0 ARM fixtures validate the shared worker, synchronous compatibility port and
actual shared-page timing/failure behavior; the native worker also passed
ASan/UBSan. The earlier 19-check compact gate was not rerun for this focused
change. See [foreground receive sessions](linux-lora-receive-session.md).

The latest shared Linux protocol maintenance connects the propagation client to
the compact composition without adding CM0 device assumptions. Direct and
propagated sends use the same core delivery planner as ESP. The compact Wayland,
framebuffer and screenshot-tool targets rebuilt successfully; all 19 compact
checks passed. Official RNS/LXMF interoperability covers the shared native
mailbox/upload implementation, as detailed in
[propagation integration](linux-lxmf-propagation-client.md). These checks retain
the existing limitation that no Cardputer Zero device is available.

The subsequent optional Linux keyboard-PWM owner is disabled without explicit
device facts, so the compact composition does not acquire CM0 wiring. The
shared menu advertises keyboard illumination only when its runtime port is
available. The complete compact rebuild completed. Its asynchronous geocaching
delete test now waits for the actual disappearance of the deleted entry with
a five-second bound instead of assuming completion 650 ms after confirmation;
the targeted rerun passed, completing the 19-check gate.
There are no known failure steps and no available target device. Review shared
production registration, service binding, keyboard workflows, and package
dependencies while preserving the compact UX and existing user data. Distinguish
an omitted Cardputer route from an unfinished shared Linux backend and from a
capability without board evidence; only the first is a target-parity regression.

## Corrections in this maintenance pass

| Finding | Correction |
| --- | --- |
| The target profile still identified its renderer as ASCII although both device entry points render shared LVGL | Select `TargetRenderer::Lvgl` and update shell validation and profile tests |
| A separate nine-page catalog duplicated production page descriptors and supplied null navigation hosts | Select the compact product's pages through `ui::app_catalog_builder`; shared descriptors now own page callbacks and menu navigation hosts |
| Protocol Probe was excluded although the shared Linux radio port and page implementation are present | Register it consistently in the production catalog selection, compact UX pack, page manifest, and screenshot capture routes |
| Shared page availability used a Cardputer-specific macro as a Linux platform test | Use `TRAIL_MATE_PLATFORM_LINUX` for Protocol Probe, walkie, and SSTV implementation selection; preserve device/UX macros for layout choices |
| The legacy session loop did not service queued quick-control intents | Pump `service_quick_controls()` outside LVGL callbacks; restore the persisted radio power setting before facade initialization |
| The package did not declare the command provider required by the new NetworkManager Wi-Fi adapter | Add `network-manager` to package dependencies and package validation |
| APPLaunch inferred a proxy from a particular local port and assumed a particular user's home | Preserve explicitly configured proxy variables and use the session home or explicit state-root override |
| The extensionless APPLaunch script could inherit CRLF on Windows and fail shell parsing | Declare LF in Git attributes, normalize the installed copy in CMake, and syntax-check the packaged launcher |

The compact shell continues using its existing settings/data locations. This
pass preserves settings, history and key tables and does not redirect existing
installations to the generic desktop composition's XDG directories. Linux device
mode now creates a persistent unique node ID instead of sharing a fixed ID;
existing peers may need rediscovery after this identity correction. An explicit
`TRAIL_MATE_NODE_ID` pins the network ID without replacing the stored identity.
Device wiring remains in
the Cardputer Zero launcher/board facts; shared Linux services do not acquire
CM0 defaults.

## Feature comparison and remaining gaps

| Production feature | Cardputer Zero status |
| --- | --- |
| Agenda / Calendar and reminders | The compact shell owns the native service and injects Calendar into the production catalog. Keyboard entry, text, date/time pickers, save, reopen/edit/delete and Back run through the real page. Time controls and location choices use separate viewports above their fixed footers. Shared reminder and Map flow binding is present, with P4 integration tests covering reminder navigation and audio-focus deferral. Durable waypoint saving and selection are connected to the shared picker through an independent native worker. Physical-device validation remains unavailable |
| Chat, contacts, team, map, GNSS sky plot, tracker, extensions, settings | Reuse shared production page descriptors and current Linux presentation/runtime adapters |
| Protocol Probe | Previously missing product registration; now enabled through the shared packet-radio port. Physical readiness still depends on the external SX1262 |
| Wi-Fi settings and control | Use the shared NetworkManager implementation, including bounded command execution and cached background status. Package dependency now matches runtime requirements |
| Radio ownership and power control | Use the shared coordinator for mesh/foreground exclusion and settings restoration. Persisted power state is restored at compact-session startup |
| Failed radio setup | Shared configuration/RX failures now invalidate readiness and block packet I/O. The adapter releases failed leases and uses bounded retry backoff, including after a slow failure; explicit settings changes may retry immediately |
| Mesh I/O ownership | Application services use the bounded mesh worker and cached snapshots. Mesh hardware/protocol work and quick-control radio switching run off the UI thread; foreground probe paths still need latency work |
| Delivery state | Result events now update the message ledger independently of the current page/protocol. SQLite reload recognizes Delivered instead of falling back to Incoming. Shutdown drains worker outcomes and the identity-broadcast action now submits a real broadcast |
| Contact public keys and local trust | The shared mesh worker now projects complete discovered MT/MC public keys into the existing directory, preserving manual pins and requiring user verification. Node observations no longer overwrite the local ignore choice. Native MC number/code verification now uses authenticated V1/V2 direct controls and the shared confirmation UI; retained MT keys cannot run an MT handshake on MC. Fake-peer/native and armhf tests pass; physical verification remains unproven |
| Calculator | Reuses the production scientific engine and page through the compact catalog. Keyboard arithmetic, result display, button bounds and return navigation are exercised in the actual shell |
| Geocaching | Linux binds a background source for local draft create/read/edit/delete and reuses core encoding/conflict rules. Cardputer menu, page manifest and stable screen binding include Geocaching. Keyboard-driven menu entry, Tab navigation, name input/cursor editing, save/edit/delete, Back and re-entry are tested. Keypad arrow keys edit text/dropdown values instead of stealing focus; encoder navigation is retained. Local Map markers now consume stored archive state and refresh after local edits without pagination-triggered rescans. Missing offline storage does not hide readable local drafts from Map. Authenticated discovery, downloads and signed publication transport remain incomplete |
| Music | The new Linux media service is enabled by explicit composition flags. Cardputer Zero does not enable it, has no music product route, and has no documented audio capability in its board facts. Audio readiness must not be inferred from the CM0 composition |
| Walkie Talkie and SSTV | Media-enabled Linux compositions now provide Codec2/WT v2 FSK walkie and a PCM SSTV receive/decoder/persistence backend. Audio-disabled compositions report unsupported. Cardputer Zero has not gained an unproven audio capability or SSTV route |
| Protocol interoperability | The shared Linux radio adapter handles Meshtastic, MeshCore group/private text/application frames, persistent MeshCore identity, signed ID announcements, local discovery and transport ACK state. Signed announcements establish peer keys; unsigned discovery responses remain provisional. Private sends currently use flood fallback. MeshCore route/path learning, remaining control/identity import-export workflows and Reticulum remain incomplete. Unsupported selections no longer transmit TML1 as a fallback. A working SX1262 packet driver alone does not establish peer interoperability |
| Fcitx5 text entry | The Wayland presenter now implements text-input-v3 with focused-editor enable/disable, bounded surrounding text, password content hints, cursor geometry, atomic commit/delete/preedit handling and stale-focus rejection. UTF-8 XKB fallback remains available when the compositor has no text-input manager. Protocol and actual LVGL/page tests cover Chinese and emoji; a real compositor/Fcitx session still needs validation |
| Notifications | Retain the freedesktop notification contract; the device notification daemon is not embedded or forked |
| GNSS | Retain configurable UART candidates and NMEA parsing for the documented Cap LoRa-1262. No physical receiver is available for validation |
| USB mass storage / PC Link | Remain excluded from the compact product menu. Filesystem access and USB device-mode exports are distinct capabilities |
| Bluetooth, camera, motion sensors | No fitted-device evidence; do not inherit CM0 capabilities. The default compact composition now opts into shared Bluetooth discovery when requested, but readiness still requires a real compatible controller and protocol backend |

This is not a claim of complete feature parity. In particular,
Geocaching transport and publication, real-session input-method integration,
physical Agenda reminders/wake, real audio workflows, and additional radio
protocols need further implementation or target-specific capability evidence.

## Software verification

The latest shared-resource/bzip2 and GNSS-selector maintenance was rebuilt in
the independent compact Debug configuration, including Wayland, framebuffer
fallback and the screenshot tool. A second incremental build refreshed changes
made during the initial long build; a final build reported no work remaining.
All 14 compact tests passed. A real CMake install into an isolated staging
prefix contained both executables, the APPLaunch entry and launcher, and the
vendored bzip2 license. The installed launcher passed `sh -n`; the installed
license matched the source byte-for-byte. This is host build/install evidence,
not a new arm64 package or physical-device validation result.

The native mesh composition now updates the contact protocol partition together
with the chat partition. Reticulum's verified person announcements use the
shared destination registry/projection and native thread-safe peer directory;
service announcements do not become nearby people. Local flags/aliases remain
owned by the directory core. Runtime observations persist on a storage thread
without holding the query mutex across I/O; explicit administrative barriers can
still wait for storage. Full-hash contact updates avoid projected-node-ID
collisions and allow a valid zero projection. See
[Linux Reticulum integration](linux-reticulum-integration.md) and
[Native peer directory ownership](linux-peer-directory.md).

The shared native chat store now implements the production durability ports:
database errors defer acceptance, LXMF full hashes and source verification facts
survive reload, and message/unread/receipt identity commit atomically. This also
fixes the compact target's inherited assumption that every SQLite append
succeeds. See [Native chat durability](linux-chat-durability.md). Plain outgoing
peer text now uses the existing queue, path/key discovery, link owner and signed
packet receipts. Authenticated plain link-text reception
uses the existing chat service and durable commit callback before its radio
proof. The same shared backend now implements encrypted unsplit text resources,
optional bzip2 compression, fair scheduling and durable completion confirmation;
custom fields, split resources, group/opportunistic messages and propagation
remain unfinished. See
[Native LXMF link-text reception](linux-lxmf-incoming.md).

The shared native Reticulum backend now also handles incoming delivery-link
handshake and control packets through the canonical session owner. Outgoing
link establishment and plain LXMF peer-text messages now use the shared worker,
including unsplit resources. Readiness still requires a usable identity and
radio; physical compact-device interoperability remains unverified. The current
compact suite remains 14/14, with native link and worker regressions covered by
the desktop suite and assertion-enabled armhf checks under QEMU. With the
durable chat-store tests, the current desktop suite passes 92/92.

Calculator now has an independent compile selector from music and a stable
compact menu/page binding. Short-display geometry keeps the scientific row and
all five keypad rows within 320 x 170. The result font fits the reduced display.
Keyboard Enter previously evaluated and then clicked the focused digit on
release; the page now ignores that synthesized keypad click while retaining
pointer and encoder activation.

The compact suite runs the existing calculator engine tests and a shell
integration test. It opens Calculator from the production menu with keyboard
events, checks visible button bounds, enters `2+3`, presses Enter, checks the
result label (excluding digit-button labels), and returns to the menu. A rendered
capture confirms the expression and result remain visible after Enter release.

The independent Debug build includes the Wayland executable, framebuffer debug
fallback, and screenshot tool. Its CTest checks cover target/shell metadata,
page manifest, compact menu/page profiles, business wiring and packaging. The
shared Linux desktop suite checks network command handling, packet
radio ownership, music/audio focus, IIO, quick controls, application paths,
display input/rotation/close handling, and P4/target profiles.

The compact suite now contains 14 passing tests. The input-method adapter is
tested over an actual in-process Wayland client/server connection with the
generated protocol: events stay pending until `done`, old editor serials cannot
insert into a new field, same-editor stale acknowledgements defer protocol
state updates, and disabled editors reject commits. Its separate LVGL test
checks UTF-8 boundaries, emoji, deletion on both sides of the cursor, password
surrounding-text suppression, preedit isolation and field destruction. Both
adapter tests run with ASan/UBSan. The production-page test submits Chinese and
emoji through the session queue, saves the draft and reopens it; preedit text
does not become persisted content.

This path uses the compositor's text-input-v3 global, not a private candidate
panel socket. Candidate rendering and the Fcitx engine remain external. The
application continues accepting unhandled XKB keys, including UTF-8 commits
to focused textareas, when the compositor does not provide that global. Runtime
verification with the target's compositor, Fcitx addon and physical keyboard
is still required before claiming multilingual device readiness.

A 320 x 170 Protocol Probe screenshot was captured with no radio present. The
page correctly renders its unavailable state and Back action. This establishes
the product route and fallback, not physical RF operation. Enabling the new
route exposed a missing portable RNode framing source in the Linux common build
list; that dependency is now included explicitly.

An explicit simulated-radio capture also renders the full compact probe page;
it is layout evidence only. A keyboard Enter on the unavailable page's focused
Back button was exercised and the lifecycle log confirmed exit to the menu.
A separate availability regression test ensures generic Linux compositions
select the implemented pages without a Cardputer device macro.

The final maintenance verification rebuilt the Wayland executable, framebuffer
debug executable, and screenshot tool in the independent Debug configuration.
All 14 compact CTest checks passed again, including Calendar and durable waypoint
keyboard flows. The platform/UI boundary guard and ESP
stack hygiene guard also passed. These checks do not replace a target-session
or physical peripheral test.

The subsequent shared Linux phone-runtime work adds a conditional GLib runtime
dependency to Cardputer's Debian metadata when GIO is compiled in. Configuration
with Debian packaging enabled verified the generated `libglib2.0-0 |
libglib2.0-0t64` alternative. This metadata check alone does not establish
physical Bluetooth support. Subsequently the Wayland executable, framebuffer
executable and screenshot tool were rebuilt against the shared phone and
application-lifecycle changes. All 14 compact checks passed again, including
Calendar, geocaching, calculator, input, manifest and packaging checks. The default
composition now opts into controller-discovered phone support rather than forcing
BLE off. Pairing policy and native MeshCore phone backend completion remain common
Linux work; no physical controller is assumed from the CM0 configuration.

The compact entrypoints also adopt orderly application re-execution. A request
exits the render loop and releases run-owned resources before replacing the
process image with the original arguments. Normal window close and automatic-exit
deadlines take priority. The initial compiler/assembler internal crash did not
reproduce when its object was rebuilt individually; the complete subsequent build
and regression run reached successful terminal states.
