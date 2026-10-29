# Linux HostLink author management

The native HostLink worker now decodes the shared canonical management envelope
and replies using both the original frame sequence and management session/request
IDs. A malformed envelope receives an InvalidParam Ack. A valid request receives
a ManagementResponse carrying the business status, followed by an Ok Ack, matching
the formal service's response/ack distinction.

HelloAck advertises `CapManagement`. The author surface consists of
Capabilities (0), AuthorInfo (1), AuthorProve (2) and SessionClose (3). The active
native connection also supplies [staged uploads](linux-hostlink-uploads.md)
(operations 32-38) when crypto support is available. Frame and
read bounds are 512 and 320 bytes, with a pending window of one. The TMS schema
field is zero because this adapter does not implement configuration snapshots.
Other operations receive Unsupported with an empty body. SessionClose is
idempotent and releases that session's transient upload owner while retaining
its durable bytes for explicit recovery or abort.

## Identity ownership and proof

Author operations create a fresh worker-owned reader of the same durable
`LinuxReticulumIdentity` database used by the native mesh worker. They do not
borrow the mesh worker's private-key object across threads or introduce another
identity store. Normal first use may create the native identity. Corrupt existing
identity metadata or key relationships fail without replacement.

AuthorInfo returns the combined 64-byte public key, identity hash, LXMF delivery
destination and nonzero generation. AuthorProve requires a 32-byte challenge and
the expected generation. It signs only the shared `deviceAuthorProofMessage`,
binding the session, challenge, both public-key halves and generation to the
fixed author-proof domain. Stale generations fail before signing. Responses
contain public information and the purpose-bound signature, never private keys.

This proves device key possession to a client. It does not authenticate a client,
authorize mutation or substitute for the pending owned-upload/install policies.
Private-key export, restore and configuration remain absent from the operation
list. Public route-package installation/status (100-101) are provided separately
by [the package coordinator](linux-hostlink-package-install.md). Processing is confined to the HostLink worker; no UI
thread filesystem access or board-specific identity is introduced.

## Verification

The native fixture checks exact capabilities, identifier correlation, persistent
identity reuse, public-key/hash/destination agreement, valid proofs, session and
challenge tampering, invalid challenge shape, zero/stale generation, malformed
requests and corrupt identity storage. Its temporary identity database is
isolated from user settings. The management, binary protocol and lifecycle
fixtures also passed as armhf executables on CM0.

The optional production-client test uses the repository's unchanged
`HostlinkConnection`, `ManagementSession` and `proveDeviceAuthor` modules over a
TCP byte-stream shim. Two sessions reuse the same request ID without crossing
responses, both verify the persisted author identity and Ed25519 proof, and
unsupported installation returns the formal business error. This validates the
production client's protocol/crypto code, not physical USB or a browser workflow.

Run `npm ci --ignore-scripts --prefix site`, configure native tests with
`-DTRAIL_MATE_TEST_WEB_CLIENT=ON`, build, and run `ctest`. The option defaults off
so ordinary C++/cross builds do not require Node or site dependencies. The opted-in
test requires Node and has a 30-second test timeout. Site dependency manifests and
lockfile were unchanged during validation.

On 21 October 2026, the opted-in native suite passed 118/118 tests (including
the production-client test); Cardputer Zero builds and 14/14 tests passed.
Platform boundary and ESP stack hygiene checks passed. The standard CM0 deb
SHA-256 is `03cb992465ee7c4378dc15aa04d3a8cf821cc8dacbe7d24177b0a50a03825239`.
Lintian reported no diagnostics. Installation and `dpkg -V` passed, the original
logo and 1232 by 568 P4 render were preserved, and compositor-confirmed focused
fullscreen Alt+F4 exited with code zero in 0.799 seconds. No driver, kernel, boot
or network-interface configuration was changed and no reboot was performed.
