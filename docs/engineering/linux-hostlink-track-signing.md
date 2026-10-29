# Linux HostLink local track signing

Linux binds formal operations 82 (prepare), 96 (V1 local signature) and 99
(V2 package signature) to a worker-owned `HostlinkTrackPrepare`. Preparation
returns a durable signed revision and retained content. Directory publication
and RF transmission are separate contracts; a successful preparation reports
`prepared=true, published=false` to the production client.

## Ownership and validation

The worker admits one bounded job. The job copies the canonical operation/upload
bindings and retains the configured storage root, author epoch and author hash.
Only the matching session's sealed upload owner can hand off its lease; an
unrelated or still-active upload remains busy. The shared storage lock then
protects input checking, revision reservation, signing and retention. The author
identity retains its protected shared ownership lease while active. Private keys
stay inside `LinuxReticulumIdentity`.

`HostlinkTrackPrepareInput` uses the existing shared unsigned candidate reader
and file checker. A candidate must belong to the current author, reference the
correct sealed upload hashes/lengths and match checked KML geometry. V2 routes
also validate the attachment index, declared resources and immutable upload
mapping through the generic attachment package checker. The native attachment
binding is shared with the existing signed-package installation input; those
entry points retain their separate unsigned and verified-signature evidence.
No local signature turns a remote author's package into a locally owned route.

All large input, XML, attachment and signing arenas belong to admitted heap
owners. The existing HostLink worker advances bounded slices; no additional
thread, unbounded task queue or whole-KML buffer is introduced.

## Durable preparation

`HostlinkTrackPreparePersistence` composes the shared checked-reservation,
prepared-signature, recovery and signed-head predicates. Paths derive from
domain-separated object/revision hashes. A first revision requires proven empty
object storage or a matching owned reservation recovery set. Unknown entries,
corrupt journals, mismatched parents, generations or operation bindings fail
explicitly. Existing committed parents are loaded and signature-verified through
the shared parent loader.

The signing port uses the shared generation-bound manifest domain primitive.
The signed blob and Signed reservation must be durable before the signed object
head is persisted. A repeated operation verifies the durable signature and
reconstructs the same result, rather than inventing another revision. Recovery
may parse the manifest in a different arena; V2 attachment proof therefore
compares the complete canonical manifest bytes before reusing the original
input evidence.

KML, index and supplied resources are then retained with the shared immutable
copy/hash/publication helper. V1 retains the formal 32 MiB / 128-file per-object
history budget; V2 retains 129 MiB / 512 files, including bounded metadata room.
Unknown/orphan files count toward admission. No Ready signature response is
emitted until required durable preparation and content retention finish.

Disconnect, session close and idle expiry release transient owners. Durable
uploads, reservation slots, signed blobs and retained data survive for an
explicit matching retry. Identity replacement is busy while preparation holds
its stages. Terminal jobs release private identity and storage leases and retain
their public result until retirement.

## Verification

The concentrated gate passed 10/10, covering formal local signing, original
package input/install, management/identity paths and track catalog/snapshots.
The complete native suite passed 250/250. The final production-client test also
passed with V1, V2 assets, V2 without assets and operation 82 responses. The
compact suite passed 19/19 and both compact runtime targets rebuilt.

The native input fixture checks V1 and V2, wrong author, stale epoch, missing
required attachment bindings, modified asset bytes and geometry mismatches. It
performs actual signature persistence and same-operation recovery. Test-only
linker interposition injects EIO into the first revision-reservation write and
checks StorageUnavailable, no Ready signature and unchanged author generation.
No production test hook was added.

On CM0, preparation/recovery, the injected write failure and original signed
package input regression passed. An independent Python TCP client exercised V1,
V2 assets and V2 without assets; system OpenSSL verified the real signatures.
It independently derived object/revision hashes, inspected retained content,
reconnected with fresh sessions, recovered identical signatures and rejected a
stale generation. All data and identities are generated in temporary roots.

The signing-delivery `/usr/bin/trailmate-linux` passed the same independent wire
checks for all three forms with dummy SDL, physical radio disabled and temporary
identity/storage roots. That release's standard deb SHA-256:
`8ea6b4213c1359338fac1c53f75b77ffad5cfcf7295f143d0c6b4c05018baf53`.
Installed executable SHA-256:
`ff5004b781dcaa138e1388ca3cf3cc6727d9b6a26619c0cac3b819b6ed5e6400`.
Lintian, installation, `dpkg -V`, original-logo comparison and isolated startup/
timed exit passed. The previous `ac736011...` package is retained as
`trailmate-cm0-before-signing.deb`. No screenshot, reboot, display-driver change,
current user identity export/replacement or user recording mutation was used.
ESP stack hygiene, Reticulum scope guard and whitespace checks passed. The
whole-worktree GitNexus audit remains CRITICAL (212 files, 847 symbols, 27 flows),
including accumulated changes; new native symbols are not all indexed and were
also audited manually. No commit was made.
The [CM0 acceptance audit](cm0-adaptation-acceptance.md) records the current artifact.

Publication operation 83, publication/history confirmation and other owned-track
management families remain separate work. These checks do not establish on-air
publication interoperability.
