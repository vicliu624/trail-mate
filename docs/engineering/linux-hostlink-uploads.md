# Linux HostLink staged uploads

The native management worker now implements TransferBegin (32), TransferChunk
(33), TransferStatus (34), TransferFinish (35) and TransferAbort (36), using the
shared upload protocol, resources, dual journal store, chunk writer, session and
seal task. Capabilities lists these operations only when the crypto-backed upload
adapter is available. An OpenSSL-free build retains explicit Unsupported stubs.

## Ownership and storage

One connection worker owns the transient coordinator. Its current upload is bound
to a nonzero session ID, upload ID, current device-author generation and SHA-256
of the combined author public key. Device identity is reread before operations;
a changed key or generation cannot continue an old upload. Another live session
cannot append, inspect, finish or abort that owner. A process-wide file lease also
prevents another application instance from concurrently admitting a writer into
the same configured storage root.

This is session/transaction ownership inside the configured HostLink connection,
not remote-user authentication. Device-author proof alone grants no mutation
rights. This stage writes quota-bound candidates only; it does not change working
configuration, private identities or published routes.

All filenames derive from the shared upload kind and 16-byte ID. The Linux I/O
adapter anchors access at a configured root descriptor, walks directory components
with `openat` and `O_NOFOLLOW`, rejects non-regular/hard-linked leaves, and uses
close-on-exec descriptors. Root device/inode checks invalidate access after media
replacement even when the pathname is reused. File data and containing-directory
metadata are synced before durable acknowledgement. New candidates are exclusive;
only validated alternating journal slots may be rewritten.

The shared per-kind limits and directory quota count unknown/orphan entries too.
The adapter supports KML, manifest, IP receipt, attachment index/resource/bindings
and signed-manifest kinds without board-specific directories or interface names.
It never takes a filename from client content.

## Progress, recovery and cancellation

Chunks are limited to 320 bytes and acknowledge the shared durable offset. Exact
duplicate chunks are idempotent; conflicting duplicates, gaps and overlapping
unacknowledged ranges fail. Journal publication follows file syncing. Recovery
uses the latest valid journal and permits a bounded unacknowledged file tail to
be overwritten from the recorded durable offset, without silently truncating
unrelated/orphan data.

Finish starts bounded background hashing on the existing worker. Polls retain
the exact transfer ID/offset; Sealed is returned only after full digest verification
and journal publication. A recovered Sealed journal is not file proof: Begin
returns unsealed progress while current bytes are rehashed. Failed hashes remain
errors and retain an owned abort path.

Close, disconnect, stop or the shared 30-second idle deadline release transient
ownership and cancel hashing while preserving durable files. A fresh Begin may
recover matching bytes under current device identity. Explicit Abort removes only
the owner's candidate and journals. Failed cleanup preserves its error state;
it never broadens deletion to unknown files.

## Verification

The C++ fixture covers duplicate/conflicting chunks, gaps, wrong sessions,
competing writers, crash-tail recovery, recovered-seal revalidation, digest failure,
abort, expiry/rebind, identity changes, all seven kinds, quota exhaustion, symlink
directories, hard-linked journals and replacement of the root inode. It uses an
isolated identity database and filesystem. The no-OpenSSL adapter also passed a
C++17 syntax check.

The production web-client test now uses unchanged `uploadTrackFile`,
`queryTrackUpload` and `discardTrackUpload` through the real management session
and native TCP worker. Upload/finish/status/abort succeed, cross-session status is
rejected, and the returned receipt remains `published: false`. The armhf upload,
management, binary and lifecycle fixtures passed on CM0. This is not a physical
USB or sudden-power-loss test.

On 21 October 2026 the opted-in Linux suite passed 119/119 tests, and Cardputer
Zero Wayland/framebuffer builds and 14/14 tests passed. Platform boundaries and
ESP stack hygiene passed. The installed standard CM0 development deb SHA-256 is
`a830901c2c80cef8a714cccfc43c2f13f9266af21310e56020db9098e786dc8d`.
Lintian reported no diagnostics; installation and `dpkg -V` passed. The original
logo and 1232 by 568 P4 render were confirmed. Focused fullscreen Alt+F4 exited
with code zero in 0.681 seconds. No kernel, display driver, boot configuration or
network-interface configuration was changed and no reboot was performed.

## Remaining work

TransferCleanup/ListOwned are now implemented in the separate
[inventory and cleanup layer](linux-hostlink-upload-maintenance.md).
Unknown or stale-identity orphan files still consume quota unless exact current
ownership can be established; they are not silently deleted. Sealing proves
received bytes only. KML/manifest/package validation,
retention and atomic route/binding publication are implemented by the separate
[installation transaction](linux-hostlink-package-install.md), which advertises
100-101 when available.
