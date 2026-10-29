# Native Wi-Fi profile storage

Linux now implements the existing ten-profile Wi-Fi configuration ports:
save/load the selected configuration, look up any saved SSID, visit profiles,
read a strict persistent snapshot and replace the complete set. Storage-only
code lives in `wifi_profile_store.cpp`; NetworkManager discovery, scanning and
connection requests remain in the native runtime. Shared page code continues
to use the same platform ports without concrete Linux classes.

Interface selection no longer uses the first wireless sysfs entry. The working
source automatically selects a single wireless adapter and rejects ambiguity.
`TRAIL_MATE_WIFI_INTERFACE` selects an explicit interface name, validated against
the wireless marker. A missing or renamed selected device never falls back to
another one. A focused temporary-sysfs fixture passed with wired and wireless
entries, multiple/renamed adapters, missing selections and invalid path/name
inputs. This change is included in the current installed CM0 development deb.
It does not establish physical AP switching.

## Automatic connection ownership

The working source now handles `connect(nullptr)` as a worker-owned saved-profile
cycle. Enabling Wi-Fi explicitly can also start that cycle. Startup/status reads
do not initiate it: NetworkManager continues to own the desktop's startup
connection. The worker reads the strict ten-profile snapshot, tries priority
order, preserves any existing connected interface, and waits for association
before considering another profile. Commands are bounded by the native process
runner, with a bounded subsequent association wait. Retry work runs on the normal
worker cadence rather than blocking the UI.

A manual connection, disconnect or disable cancels the automatic cycle. Pending
user commands take precedence over the next saved profile. A cycle remains tied
to its initially selected interface; a different discovered interface cancels it
instead of migrating credentials to that adapter. Exhausting the saved set stops
app retries and publishes an error until a new connect/enable request. Unlike
the ESP driver's radio shutdown on exhaustion, Linux leaves the system-wide
NetworkManager radio switch and other interfaces under desktop ownership.
Saving profiles does not initiate connections or rewrite their priority after
an association attempt. These changes are included in the installed deb with
SHA-256 `3375ba644ebc9b6d4cdae61022fc0b8b6951c48efec037e422390e44b98da674`.

The actual private worker is exercised with link-time NetworkManager/process
and snapshot fakes, without additional production test hooks. Eight scenarios
cover priority/fallback, desktop-connection preservation, exhaustion without
system radio changes, disconnect, disable, manual override, interface changes,
and disabled preferences followed by explicit enable. The worker, interface
selection and existing parser/process fixtures passed on the native host and
as armhf executables on CM0. These runs did not activate or disconnect any real
NetworkManager connection. The ARM build completed with exit code zero. Physical
AP switching and authorization remain outside this mock-backed evidence.
The preceding `de4fa2d...` release gate passed all 203 native tests and all 19 Cardputer Zero tests in
single complete runs. The exact standard armhf package passed lintian, CM0
installation, `dpkg -V`, original-logo comparison and the formal application
HostLink/configuration/exec/receipt-replay/exit scenario. That installed-app
scenario did not request a Wi-Fi enable or association. The subsequent RF-only
package retains this network implementation, passed focused native/ARM fixtures
and installed-app startup/exit validation; it did not repeat that complete gate.

The existing ESP snapshot admission algorithm now lives in the platform-neutral
`wifi_profile_snapshot.h`. The original ESP include/API delegates through a
strict-reader adapter. The rules retain bounded SSID/password fields, explicit
count validation, duplicate rejection, historical indexed-password aliases and
legacy primary-pair recovery. ESP retains its disabled default. Linux supplies
its existing enabled default when the preference is absent because desktop
networking owns startup state. Snapshot capture does not change system Wi-Fi.

The native repository uses the existing `settings` keys and value kinds, not a
new document format or table. Saving a nonempty SSID promotes it to the first
priority, updates its credentials and retains other entries. The least-priority
entry is evicted when a new SSID fills an already full set. Replacement preserves
the supplied order; an empty replacement clears all slots, old password aliases
and primary credentials. The selected primary pair remains mirrored for older
callers. Profile lookup uses exact SSID bytes and returns the saved global
enabled preference.

Native profile mutations commit count, enabled state, all indexed slots,
historical-alias cleanup and the primary mirror in one SQLite transaction. A
failed mutation rolls the entire set back. A profile-owner mutex serializes
read/modify/write saves so concurrent additions cannot lose each other. The
transaction backend shares the existing settings-store lock and committed-reset
fence. A reset rejects subsequent stale profile mutations during teardown.

`SettingsReadView` provides strict reads against one transaction. The shared
snapshot algorithm consumes that view, so concurrent database commits cannot
mix profile versions in one result. Visitors receive an owned native snapshot
after the transaction/lock ends, allowing visitor code to use ordinary settings
APIs without recursive locking. The view is noncopyable and scoped to its
callback; callbacks must use the supplied view rather than other store APIs.

The regression uses actual SQLite storage, a real late-write rejection trigger,
an independent writer committing midway through snapshot capture, concurrent
saves and a fresh executable process reopening all ten entries. It checks
legacy recovery, lookup, order, eviction, replacement, explicit empty lists,
32-byte UTF-8 SSIDs, 64-byte passwords, invalid input clearing and reset fencing.
The native profile/store implementations are instrumented with address and
undefined-behavior sanitizers in the desktop fixture.

On 22 October 2026 the full native suite passed 154/154, Cardputer Zero passed
19/19 and HostLink/TMS passed 126/126, including the unchanged ESP/IDF snapshot
contracts. The native profile fixture passed ten consecutive runs. Profiles,
strict settings, reset and legacy configuration migration ARM fixtures passed
on CM0. The ESP stack guard passed; a complete ESP firmware build was not run.

On the installed CM0 package, injected pointer/keyboard input saved two fake
network credentials through the shared P4 Wi-Fi form. SQLite retained both
complete pairs, with the most recent first. A fresh process reopened the page
and retained both pairs. The system Wi-Fi switch and active connection snapshot
were unchanged before/after each UI run. Tests used isolated configuration roots
and did not press Connect/Disconnect or change system networking.

Actual AP connection/switching and automatic profile failover were not verified
by the historical storage tests above. The worker integration described earlier
adds profile cycling; its test scope is distinct from a physical AP test.
Complete current-schema TMS import/export was subsequently implemented; see
[TMS transaction boundaries](linux-tms-transaction-boundary.md).

The standard armhf deb was installed on CM0, SHA-256
`05b517d44bee7dce825ac58d672084511b9e3216c885838e0fb48b4de6ccc18e`.
Lintian produced no diagnostics; `dpkg -V`, the original logo hash and the
installed Crypto license comparison passed. The installed P4 homepage and
active packaged map passed fullscreen/focus and Alt+F4 exit with code zero in
0.649 and 0.639 seconds. The previous package is retained as
`trailmate-cm0-before-wifi-profiles.deb` in the device validation cache.
