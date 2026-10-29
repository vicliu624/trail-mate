# Linux HostLink identity backup and replacement

Formal identity backup export (4), restore (5) and restore-format negotiation (6)
are connected through the Linux HostLink worker, application owner and existing
management envelope. Export requires crypto support; restore is advertised only
when the matching database has a live application/lifecycle owner. Both seed and
expanded formats are supported. The current installed artifact is recorded in
the [CM0 acceptance audit](cm0-adaptation-acceptance.md).

## Representation and ownership

The backup uses the production `trailmate.identity.backup.v1` signed CMP payload
and the `TrailMate.Identity.Backup.v1` signature domain, including its terminating
NUL. The payload binds representation, combined public key, private key, identity
hash, delivery destination and author generation. The existing browser client
verifies this signature and encrypts the resulting backup; the native transport
does not write an unencrypted backup file.

| Backup format | Private representation | SQLite record version |
| --- | --- | --- |
| 1 | X25519 private32 followed by Ed25519 seed32 | Existing version 1, 64 bytes |
| 0 | Clamped X25519 private32 followed by expanded Ed25519 private64 | Version 2, 96 bytes |

Expanded signing material contains a scalar and nonce prefix; it cannot be
treated as a seed. Existing version-1 records remain readable without rewriting
their keys. New version-2 records validate the encryption/public relationship
and expanded signing/public relationship. Native expanded signatures are also
checked through the independent OpenSSL verifier in the replacement fixture.
Invalid shapes, unknown record versions and key mismatches are rejected rather
than regenerated.

Each current `LinuxReticulumIdentity` owner holds a protected, close-on-exec OFD
read lease beside its SQLite database. Replacement requires a nonblocking
exclusive upgrade, which refuses another live owner, including a separate
process. These are advisory cooperating-owner locks, following the
[Linux OFD locking contract](https://man7.org/linux/man-pages/man2/fcntl_locking.2.html).
All live application identity users must still be quiesced by the composition;
the file lease is not a substitute for that lifecycle. Older binaries that do
not implement this lease must be stopped before an upgraded restore is used.

The replacement primitive validates both key pairs and compares the expected
public key/generation against freshly read durable state inside `BEGIN IMMEDIATE`.
It writes the complete key representation and increments generation in the same
SQLite transaction, including when restoring the same key. Rejected or rolled-back
updates preserve the old identity. A committed or uncertain write clears the
old private arena and permanently retires that object; its `begin()` cannot
silently activate the replacement. The exclusive lease remains until that owner
is destroyed. The composition must arrange restart/reconnection and fresh proof
before claiming the new identity is operational.

## Application restore lifecycle

The worker validates consent, backup signature, both key pairs and derived
addresses before queuing an owned command. One bounded bridge marshals it to the
frontend. A request that has not started expires without applying; an applying
request waits for a definite outcome. Unbinding cancels queued work and waits for
an applying callback without losing its terminal result. Buffers are wiped when
the command owners retire.

Active uploads, package publication, configuration snapshots/transfers and the
call configuration gate refuse a restore. Busy voice operations also refuse
frontend admission. Stale target generation/public key is checked before stopping
services and again under the storage transaction. Once admitted, the frontend
stops voice and the resident protocol worker, destroying its old identity users
and sessions, before the atomic replacement. Further frontend ticks and settings
mutations are fenced for the retiring run.

After quiescence, every outcome requests an application-only restart: successful
or uncertain writes open the replacement after exec; rejected/rolled-back writes
reopen the old identity. The business response still reports failure for a failed
write. Invalid requests, stale targets and admission refusals do not retire the
run. Window-close/Alt+F4 exit intent retains precedence over automatic restart.
A lost response is not a failed-write verdict: reconnect and verify the expected
public key, addresses and next target generation through a fresh author proof.

## Export contract

Export reuses the shared request parser: a nonzero expected generation and an
explicit true consent field are required. Stale generation and malformed/denied
consent return the existing business errors. It takes a fresh identity owner for
each request, without retaining private key bytes in the coordinator.

Private export is for an already trusted management link. Device-author proof
does not authenticate the client, and the consent field is not independent
network authentication. Native HostLink retains its existing loopback default;
external binding remains an explicit composition choice.

The native payload workspace, encoded management workspace and response vector
are wiped after use. The bounded transmit queue wipes a completed slot and all
slots on disconnect/reset/destruction. Partial writes retain bytes only while
the transport still needs them. No private fixture bytes are printed or copied
to the package/route fixture exports.

## Current verification

The storage gate passed 4/4: new replacement tests, existing identity/RFC vectors,
the no-OpenSSL identity path and the production author-client fixture. Replacement
tests cover seed/expanded keys, stale targets, invalid pairs, another in-process
owner, a separate reader process, injected SQLite failure/rollback, writer
retirement, reopening, same-key generation advance and exhausted generation.

The subsequent export gate passed 4/4: signed export/restore-parser compatibility
for both formats, existing management semantics, transport queue partial-write
and wiping behavior, and the production browser-client TCP fixture. The browser
fixture uses unchanged `exportEncryptedDeviceIdentity` and
`decryptIdentityBackup`, checks signature/address/public metadata, rejects a wrong
password, and rejects denied consent and stale generation at the device.
The storage and export fixtures instrument their relevant units with ASan/UBSan;
this does not imply every common library translation unit is instrumented.

The restore integration gate passed 7/7, including actual `LinuxAppServices`
instances, real process exec, formal browser export/preview/restore/re-proof,
both formats, same-key generation advance, call/configuration and upload refusal,
stale/tampered requests, injected write failure and deliberate response loss.
Signed but inconsistent identity/address/private fields are rejected. The full
native suite subsequently passed 222/222 in one run; both compact targets built
and Cardputer Zero tests passed 19/19.

Five ARM groups passed on CM0: replacement, signed backup, frontend bridge,
identity/RFC vectors and no-OpenSSL behavior. An independent Python TCP client
also exercised both formats and write-failure recovery through real exec,
discarding the commit responses and verifying fresh Ed25519 author proofs with
the system OpenSSL command. Signed messages use transient memfd storage; only
public verification material is written to fixture files.

The exact installed `/usr/bin/trailmate-linux` additionally passed an isolated
P4-profile seed restore, actual exec/endpoint replacement and fresh signature
proof. This run uses dummy SDL, radio-disabled and keyboard-PWM-disabled test
profiles; it does not claim physical UI input or RF validation.

The previously validated standard deb SHA-256 was
`8ea6b4213c1359338fac1c53f75b77ffad5cfcf7295f143d0c6b4c05018baf53`.
Its installed executable was
`ff5004b781dcaa138e1388ca3cf3cc6727d9b6a26619c0cac3b819b6ed5e6400`.
Lintian, installation, `dpkg -V`, original logo and installed startup/timed exit
passed. The previous package is retained as `trailmate-cm0-before-signing.deb`.
All identity mutations use temporary generated test identities; the current
user identity was not exported or replaced. No device reboot, display-driver
change or screenshots were used. ESP stack hygiene and Reticulum scope guards pass.

The subsequent radio-management release passed the complete native suite
234/234 and compact suite 19/19. Author seed, expanded and write-failure TCP/exec
regressions passed again on CM0. Radio operations use a separate coordinator
while reusing this frontend lifecycle bridge; see
[wireless identity storage](linux-radio-identity-storage.md).
