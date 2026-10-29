# Linux settings factory reset

The shared Settings confirmation now enters the platform-owned reset port on
Linux before clearing settings and requesting an application restart. The
Linux binding belongs to `LinuxAppServices`; it detaches before destruction
and cannot be detached by a retiring, different owner. Page code contains no
Linux database paths, board identifiers or concrete service casts.

`settings_reset_policy.h` retains the existing nine settings namespaces in one
shared list. The Linux repository resets those namespaces together with its
authoritative AppConfig and Reticulum network namespaces in a single SQLite
transaction. It writes a fresh default AppConfig blob and a completed-reset
marker before committing. The network projection is removed so the next
process uses the normal factory-default policy. A late SQL error rolls the
entire operation back; neither settings nor persistence admission change.

The existing Linux AppConfig backend stores a versioned object representation.
Reset constructs defaults in cleared, aligned native storage and retains the
same blob version. It does not introduce another configuration format. This
remains separate from the unfinished complete native TMS import/export work.

After commit, settings-store writes and removals targeting the reset namespaces
are rejected for the remainder of that process. Numeric preferences, strings,
blobs and old namespace-clear requests use the same serialized guard. The
service discards pending AppConfig work, rejects later saves/network edits and
detaches its editable network projection. `finish()` keeps the committed reset
quiescent through orderly teardown. The standard application lifecycle then
executes the current executable again, preserving arguments and environment;
the new process resumes ordinary persistence. It does not reboot Linux.

The completed-reset marker also suppresses the one-time SD group migration.
An old `groups.tsv` cannot repopulate reset groups, including if the working
AppConfig blob later becomes missing or invalid. The migration file need not
be mounted or writable during reset. Existing message, identity and archive
storage remains under its existing ownership; this is the Settings operation
labelled “Clear all settings and restart?”.

The regression injects an actual SQLite delete rejection after earlier
namespace deletes and checks complete rollback. It then commits reset, attempts
stale writes/removals through the production settings APIs and performs ordinary
service teardown. Two real application `exec` restarts retain the same PID,
load defaults, preserve unrelated SQLite records, suppress the old SD migration
and resume configuration writes. The second restart also tests an intentionally
removed working blob with the completed-reset marker retained.

On 22 October 2026 the full native suite passed 152/152, including ten consecutive
reset runs. Cardputer Zero passed 17/17 with the same reset regression in its
media-disabled composition. Reset, multi-endpoint settings and configuration
persistence ARM fixtures passed on CM0. `git diff --check` and the ESP stack
hygiene guard passed. A complete ESP firmware build was not performed; the
Arduino reset path retains its existing sequence and shared namespace list.

The standard armhf deb was installed on CM0, SHA-256
`466ad695f983ebee8e8f3b1f8c491c17049dca768519d0f89bdb93d9a492b7b2`.
Lintian produced no diagnostics; `dpkg -V`, the original logo hash and the
installed Crypto license comparison passed. An isolated copy of the earlier
UI fixture contained a custom gateway, an old username, a scalar sentinel and
an SD migration source. Compositor-injected input opened the shared P4
Maintenance page and confirmed Factory Reset. The application emitted a second
boot sequence under the same PID, loaded defaults and removed the old network
projection/sentinel. The default username was reviewed in the restarted P4
page. The kernel boot ID remained unchanged. Alt+F4 exited with code zero.

The installed homepage and active packaged map also passed fullscreen/focus
and Alt+F4 exit with code zero in 0.982 and 0.486 seconds. The prior package is
retained as `trailmate-cm0-before-settings-reset.deb` in the validation cache.
The reset UI test used verified paths below the device validation cache; it
did not reset the user's normal application configuration.
