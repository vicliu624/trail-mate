# Linux HostLink upload inventory and cleanup

The native upload adapter now implements TransferCleanup (37) and TransferListOwned
(38), and lists them in the active connection's management capabilities. It uses
the same shared upload descriptors, paths and journal recovery as staged uploads.
The changes do not add a board ID, hardware path or another transfer protocol.

## Exact cleanup

Cleanup takes the original Begin tuple: upload ID, kind, byte length and SHA-256.
An active owner must match that tuple, current author/generation and session;
another active ID is Busy. With no transient owner, the adapter acquires the same
cross-process upload lease and loads both journal slots under current identity.
Only a recovered matching binding permits deletion of the three derived paths.

Candidate content may be absent or damaged: intact ownership journals still allow
repairing that transaction. A valid surviving journal can recover a torn alternate
slot. Wrong descriptors, another identity/generation or no valid ownership journal
do not authorize deletion. When both journals and candidate are already missing,
cleanup succeeds idempotently. An orphan candidate without journals remains a
Conflict even when its filename is known.

Abort and Cleanup share one confined removal routine. It removes the candidate
before its journal slots, syncs directory changes and checks root identity.
Interrupted cleanup therefore retains an ownership journal until the candidate
is gone. It never enumerates unrelated files for deletion or removes installed
package retention data.

## Live inventory

Requests use `[kind, null | [media, position]]`; responses are
`[media, next | null, row | null]`. Rows contain ID, kind, total bytes, file hash,
generation, recorded phase and durable offset. Both journal names may yield the
same ID, so clients deduplicate. Recorded Sealed is metadata, not fresh file proof;
Begin still rehashes recovered sealed bytes.

Inventory is unavailable while a transient upload owner is active. A page takes
the cross-process lease, examines up to four new directory entries and emits at
most one row. Native cursor positions are 32-byte-aligned logical entry ordinals;
the adapter bounds its rescan to the shared maximum upload directory population
of 192 entries. It does not retain an unbounded filename list or accept native
directory cookies from clients.

The opaque cursor binding includes root device/inode, current session/author,
generation and directory metadata revision. A stale/forged binding, out-of-range
position or observed directory mutation returns Conflict so the caller rescans.
Filesystem handles and exact descriptor-relative path checks remain authoritative;
the cursor is not an authorization credential or a signed snapshot. Non-journal
and special/link entries are not promoted into owned rows. Other identities'
records remain untouched. Corrupt unowned data is not silently cleaned.

## Verification

The native fixture covers pagination and duplicate rows, active/competing owners,
wrong session/hash/length/ID, missing candidate, idempotent cleanup, journal-less
orphans, both corrupt slots, a valid surviving slot, identity changes, root
replacement, directory mutation and invalid cursors. The production web client
also uploads an object, releases transient ownership, lists the recorded sealed
upload and performs exact cleanup twice; a wrong digest fails first.

The maintenance, upload, management, binary and lifecycle fixtures passed as
armhf executables on CM0. This is not physical USB or sudden-power-loss testing.
Installation of a signed route package is now implemented as a separate
[package transaction](linux-hostlink-package-install.md); inventory and cleanup
do not confer publication authority.

On 21 October 2026, the opted-in native suite passed 120/120 tests and Cardputer
Zero Wayland/framebuffer builds and 14/14 tests passed. Platform boundaries, ESP
stack hygiene and the no-OpenSSL C++17 syntax check passed. The standard installed
CM0 deb SHA-256 is `dd2f5ec04340f4cccdfde923f2a5dcbfadf47437c8569dcc7376770394b9c775`.
Lintian reported no diagnostics. Installation and `dpkg -V` passed; the original
logo and 1232 by 568 P4 render were preserved. Compositor-confirmed focused
fullscreen Alt+F4 exited with code zero in 1.141 seconds. No driver, kernel, boot
or network-interface configuration was changed, and no reboot was performed.
