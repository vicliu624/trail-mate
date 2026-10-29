# Linux wireless identity storage and ownership

Linux HostLink implements formal Meshtastic/MeshCore identity operations 7-9:
read identity information, export a signed backup, and explicitly restore a key.
Restore is advertised only when the matching frontend lifecycle owner is active.
The current delivered artifact is recorded in the
[CM0 acceptance audit](cm0-adaptation-acceptance.md).

## Shared ownership lease

`IdentityOwnerLease` centralizes the protected Linux OFD lease implementation
already used by the device-author identity. Owner files are user-owned regular
files, mode 0600, no-follow and close-on-exec. Shared leases allow cooperating
readers; exclusive upgrade refuses other live owners without dropping the
requester's read lease. A lease object remains bound to its normalized path.

The author identity retains its existing lease path and behavior. MeshCore
identity objects and Meshtastic PKI caches hold the radio lease for their cache
lifetime. Radio replacement quiesces these owners before obtaining
exclusive access. Snapshot reads hold their own short-lived shared lease. These
are cooperating-owner locks, not protection against arbitrary direct SQLite
mutation or older binaries without the lease protocol.

## Read-only snapshot

`LinuxRadioIdentityStore::read` opens one SQLite read transaction for the key
pair and optional generation metadata. It never generates an identity or rewrites
its private encoding. Namespace/public/private selectors remain explicit for
the primary Meshtastic store; protocol table names describe the existing native
storage schema rather than a board or device-node index.

| Kind | Existing private storage | Snapshot representation |
| --- | --- | --- |
| MeshCore (1) | Legacy 32-byte seed, or explicitly imported 64-byte expanded key | Validated 64-byte expanded Ed25519 private key and matching public32 |
| Meshtastic (2) | Existing public/private32 settings BLOB pair | Validated X25519 pair; a canonical clamped private copy for wire backup |

The MeshCore loader now accepts both shapes while preserving old seed records.
Expanded material is validated against its public key without attempting seed
recovery. Older seed-only binaries do not understand the expanded shape; this
must be considered when an explicit restore writes that representation.
Meshtastic normalization affects only the transient snapshot, not the stored
legacy bytes.

Generation zero denotes a legacy/unmanaged key or an empty slot, matching the
formal radio protocol. The snapshot reads optional
`linux_radio_identity_generation(kind,generation)` metadata without creating it.
Missing tables/slots return a valid absent snapshot; malformed shapes, key
mismatches, invalid generation records and unavailable storage fail explicitly.
Failure clears the complete caller result, including private bytes. Private
snapshot arenas are wiped on destruction.

Meshtastic initialization now generates a new key only after a genuine NotFound.
Corrupt/unavailable records and mismatched key pairs no longer trigger automatic
replacement. Cached private bytes and temporary bootstrap copies are cleared
when their owners retire.

## Verification and current boundary

The native gate initially passed 7/8. Its only failure was the existing MeshCore
diagnostic-text assertion; the generic private-key mismatch wording was corrected
to retain the established `match` contract, and that one fixture then passed
separately. The other seven were not repeated for the text-only change.

Coverage includes the new snapshot/ownership fixture, existing MeshCore
identity/group fixtures, author-identity replacement/RFC/no-OpenSSL fixtures,
actual MQTT application/private chat and the formal author restore/exec browser
fixture. The new store and lease units are instrumented with ASan/UBSan; this
does not claim instrumentation of every linked common translation unit.

The new fixture checks absent reads without key/database creation, seed and
expanded MeshCore compatibility, signature verification through OpenSSL,
exclusive-owner refusal, canonical X25519 export copies without storage rewrite,
generation metadata validation, and preservation of corrupt/mismatched
Meshtastic records through an actual injected-radio adapter initialization.
ESP stack hygiene and Reticulum scope guards pass.

## Restore transaction and lifecycle

The separate radio coordinator reuses the shared formal backup codec and the
existing bounded frontend restore bridge. Export requires explicit consent and
binds the signed payload to the source author, radio generation and request.
Restore validates the signature and canonical key pair before dispatch.

The frontend checks active transfers and realtime work, then quiesces radio and
voice owners. The store obtains exclusive radio ownership and checks the expected
author generation/public key and radio generation/public key inside the same
SQLite write transaction as the key and generation update. A stale author, stale
radio, concurrent owner or failed write cannot partially replace a pair. The
author identity remains unchanged. Restoring the same key advances the radio
generation while preserving its existing private encoding. A committed restore
restarts the application; failure after quiescence also restarts with the old
identity. A lost response requires fresh connection and identity verification.

## Delivered verification

The concentrated management gate passed 12/12, the complete native suite passed
234/234, and the compact suite passed 19/19. ARM application, audio/hardware tools
and identity fixtures built successfully. On CM0, snapshot, six replacement
scenarios and four atomic local-store scenarios passed. Independent TCP clients
verified both radio kinds, actual exec, fresh author signatures, lost responses
and rollback using system OpenSSL. The author seed/expanded/write-failure
regressions also passed on CM0. All identities are generated under temporary
roots; no current user identity or physical radio was changed.

The standard deb passed lintian, installation, file/logo integrity and isolated
startup/timed exit. ESP stack hygiene and Reticulum scope guards pass. These
checks establish storage and management behavior, not on-air interoperability.
