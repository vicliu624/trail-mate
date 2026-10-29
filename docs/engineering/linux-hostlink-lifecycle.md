# Linux HostLink lifecycle and implementation scope

The native HostLink service listens on a configurable TCP endpoint, defaults to
loopback and writes an endpoint locator under the configured SD root. The legacy
text greeting/echo exchange has now been replaced with shared `HL` binary framing,
Hello/status and correlated command rejection; see [binary transport](linux-hostlink-binary.md).
This is not a Linux USB device transport. Basic binary interoperability must not
be counted as management or route-install interoperability.

## Reproduced failure and repair

An invalid bind address made the worker terminate with an error. `stop()` then
returned early because the running flag was already false, leaving a joinable
thread. Static destruction aborted the process with `terminate called without
an active exception`. The isolated child-process regression failed before the
repair and passed afterwards.

An owning worker now stops before its mutex, state and string dependencies are
destroyed. Start/stop are serialized independently from status access. Stop
always joins an existing worker, even after startup failure. The worker owns
socket closure; cancellation is observed through the existing bounded poll.
The control thread no longer closes a descriptor while the worker may be using
it, avoiding descriptor reuse races. Thread creation failures become service
errors. Linux socket writes suppress SIGPIPE. Fallback port selection cannot
wrap above 65535 into an unintended port-zero listener.

`LinuxAppServices::shutdown()` explicitly stops HostLink before resource,
radio and storage service teardown. The process-exit owner remains a fallback.

## Verification on 21 October 2026

The isolated fixture covers failed listener cleanup and process exit, repeated
start/stop, four concurrent controllers with ten cycles each, and process exit
while a listener is active. It also checks endpoint-file cleanup and stopped
status. An additional public application-service shutdown check passed natively
and on CM0, confirming the listener is stopped before process exit. The fixture
passed natively and as armhf code on CM0. The full native
suite passed 115/115; Cardputer Zero Wayland/framebuffer builds and 14/14 tests
passed. Platform boundary and ESP stack guards passed.

The standard CM0 deb SHA-256 is
`821dd0285f55d04ccdb32de53e113a21fe315a33d8ddfef23566aa847075fa64`.
Lintian reported no package diagnostics. Installation and `dpkg -V` passed;
the original logo and 1232 by 568 P4 render remained correct. Compositor-confirmed
fullscreen/focused Alt+F4 exited with code zero in 0.699 seconds. No driver or
boot configuration was changed and no reboot was performed.

## Remaining formal management work

Reuse the shared frame decoder, router, service codec and management envelope;
do not invent a native-only route-import protocol. Formal package installation
also requires actor/session authorization, sealed uploads with ownership and
hash checks, prepare/status/expiry behavior, retained original signed package
bytes, publication policy and atomic route/binding publication. Existing shared
package checks and the native signed-image reader provide validation building
blocks, but neither supplies this missing transport/transaction service.

The relevant formal operations include `TransferBegin` through `TransferFinish`,
`TrackPackageInstall` (100) and `TrackPackageInstallStatus` (101). The current
binary transport does not yet implement these operations. It now advertises
`CapManagement` for operations 0-3 only; see
[author management](linux-hostlink-author-management.md).
