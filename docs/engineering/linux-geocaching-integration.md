# Linux geocaching integration

## Existing boundaries

The shared geocaching page consumes `ui::geocaching::Source`. Snapshot, item and
command methods must not perform disk or network I/O on the UI thread. The ESP
`browse_runtime` owns the business composition but also depends on its storage
maintenance scheduler, SD wrappers, Reticulum/LXMF adapters and board services.
Copying that implementation into Linux would duplicate the business state and
retain platform dependencies.

Portable core headers already define draft encoding, validation, identity
restoration, optimistic edit conflicts and signed publication requirements.
Linux adapters should reuse these contracts. UI registration alone cannot make
geocaching functional.

## Native draft storage port

`LinuxGeocachingDraftStore` is a blocking storage-owner port. Its database path
is injected by the composition; it has no CM0, Cardputer, username, device-node
or home-directory defaults. It stores the core MessagePack draft bytes in a
dedicated SQLite table. This is local persistence, not a new radio wire format
or a replacement for the core signed cache/publication records.

An immediate transaction covers the committed read, expected-generation check
and save. Concurrent connections cannot both replace the same generation.
Editor bytes cannot supply author/base metadata. The adapter restores committed
identity fields using the core helper. A draft with a retained-publication base
returns `PublicationRequired` until publication retention is implemented; it
does not discard that base to permit an edit.

Delete leaves a local tombstone so a stale new/edit command cannot resurrect
the same draft ID. Reads exclude deleted rows. Paging returns four bounded
summaries with coherent count metadata, rather than retaining a list of long
descriptions. Invalid input and corrupt committed records remain distinct from
missing rows. Failed writes roll back without changing the prior generation.

The native test covers core-codec persistence across reopen, two concurrent
edit connections, stale/new generation rejection, deletion/tombstones, bounded
paging, injected write failure/rollback, publication metadata rejection and
retention, and corrupt committed rows. The same test executable is compiled
for armhf and run under QEMU against the target SQLite libraries. Native tests
also use ASan/UBSan.

## Remaining composition work

- Implement the remaining authenticated cache/publication workflows below.
- Integrate authenticated directory discovery, queries, downloaded cache
  verification, signed publication/history and Reticulum/LXMF transport.
- Complete local downloaded-copy removal and publication/history workflows.
- Complete the remaining protocol workflows before claiming feature parity.

## Native record verification

`LinuxGeocachingCrypto` implements the core `RecordCrypto` port using the existing
OpenSSL dependency: SHA-256 and Ed25519 verification. The core retains signature
domain separation, envelope decoding, cache ID/revision hash construction and
expected-ID/hash matching. The adapter owns no identity, private keys, radio
configuration, publication state or directory-trust policy.

The native tests use the production checked-in signed-cache fixture and its
expected ID/hash bytes. They check valid verification, signature tampering,
identity/hash mismatches, malformed/trailing envelopes, insufficient workspace,
invalid input spans, empty SHA-256 input and overlapping hash input/output.
A separately compiled executable omits OpenSSL and must report unavailable
instead of accepting a record. Hash failures clear the output. Both enabled
and unavailable variants are also compiled for armhf and executed under QEMU.

This is a prerequisite, not an authenticated directory/download implementation.
Cache persistence must check retained history using the core version policy,
not merely compare the incoming record with the current head. A valid author
signature alone does not prove directory trust, current revision or delivery.

## Verified record history repository

`LinuxGeocachingRecordStore` persists signed records and a selected-head
projection in one SQLite transaction. The path and `RecordCrypto` dependency
come from the owning composition; it has no device/profile defaults. It is a
blocking storage-owner port, not a UI callback implementation.

Every admission checks the expected cache ID/revision hash and signature with
the core verifier, revalidates retained records and compares incoming versions
using the core version policy. Sorted retained history is also checked for
forks, creation-time changes, broken adjacent links and records after an archive.
Older records can fill history gaps without replacing the current head. A
complete contiguous chain clears the incomplete-history projection.

The head also retains the committed history-row count. A missing historical row
is detected even when the chain already had a gap; a later admission cannot
silently make lost evidence part of a newly accepted incomplete chain.

Conflicting signed records are retained atomically and block head reads. A newer
record cannot erase that conflict. Head reads recheck historical signatures,
references and completeness; corrupt/missing historical records are not treated
as an empty repository or silently downgraded to an incomplete chain. Historical
reads remain available by exact ID/hash for valid retained evidence.

The repository deliberately has no history-deletion API. Removing an offline
installation must remove a separate local-installation reference, preserving
the version evidence needed to reject rollback. Record retention does not prove
that a GPX file is installed, a directory is trusted or a publication delivered.

Tests reproduce the production fixture byte for byte using its public test seed,
then sign linked revisions, gaps, forks and archived/creation-time conflicts.
They cover reopen, out-of-order history repair, invalid bindings/signatures,
sticky conflicts, missing/corrupt history, orphan references, injected head-write
failure with full transaction rollback, unavailable crypto and two concurrent
writers. The native repository test uses ASan/UBSan. The same scenarios run in
an armhf executable under QEMU.

This port is compiled in the Linux common library but is not yet connected to
the background source's network/download path. Local-draft behavior is unchanged.

## Native GPX staging

`LinuxGeocachingGpxStage` prepares the existing core GPX representation from a
signed record bound to the expected cache ID and revision hash. It is a blocking
storage-owner port with an injected existing cache root. It never creates a
missing volume root or selects a device/home directory.

Staging files use nonzero transaction IDs under `.state/staging`. Linux opens
state/staging directories relative to descriptors without following symlinks, creates files
exclusively, synchronizes contents and directory entries, reopens the configured
path, and compares every readback byte with the core serialization. Successful
proofs contain a relative path, byte count and SHA-256 of that GPX. Nonregular or
multiply linked files are rejected; FIFO verification uses a nonblocking open.
Existing/partial files are never truncated or automatically removed. Explicit
`verify` revalidates a retained stage against the authenticated record.

Native ASan/UBSan tests cover exact serialization/hash, restart verification,
damaged/partial/trailing files, same-transaction collisions, missing roots,
symlinks, hardlinks, FIFO handling, unavailable crypto and independent roots.
A process-local file-size limit produces an actual short write/EFBIG and checks
that failure clears the proof while retaining the partial file. The same cases
are compiled for armhf and run under QEMU.

This is staging only. The file port below binds the active generation, prepared
journal/backup, file revalidation and installation commit. Its staging calls use
the duplicated descriptor of the leased root. A staging proof alone never sets
Downloaded state. Application Source/network composition is still pending.

## Installation journal metadata

`LinuxGeocachingInstallJournal` stores volume-scoped intents, unsigned 64-bit
generations and core-encoded install records in a durable SQLite file. The
volume ID and crypto provider are injected. In-memory database URIs and zero
volume/task IDs are rejected. The source of a selected summary must still be
authenticated/correlated by the transport owner.

An intent can be reserved before its signed Get response arrives. Prepare and
commit then recheck verified history, the selected record and the core
`installableRecord` policy against the previously installed record. New/old file
hashes bind the prepared journal; observed hashes must match at commit, rollback
and cleanup. Superseded generations cannot mutate the journal. Cancelled
prepared work and installed work with unfinished cleanup remain available to a
stable task-key recovery cursor. Unresolved preparation/conflict/cleanup blocks
another installation for that cache instead of dropping recovery evidence.

The volume's installed revision high-water mark is separate from the history
repository's newest observed signed record. Observing v3 does not invalidate an
active v2 installation. The core policy can reject a direct installed-v1 to v3
jump while still permitting v1 to v2 to v3. Installed high-water only advances at
commit and survives removal of the local-copy reference, preventing rollback
without permanently blocking the intermediate revision needed for an upgrade.

Installing the new reference and changing the journal to Installed occur in one
transaction. An injected failure in either write rolls both back. Counter bytes
retain the full unsigned range and cannot wrap. Native ASan/UBSan and armhf/QEMU
tests cover reopen, pre-response reservations, cancellation, stale callbacks,
prepare/commit/cleanup/rollback, volume isolation, malformed journal records,
write failures, unavailable crypto, concurrent allocation, wide counters and
counter exhaustion. The test file hashes are metadata fixtures, not observations
of installed files.

This component performs no filesystem replacement. The volume guard and file
port below implement those effects and the shared `GpxInstall` driver contract.
Journal phase flags alone must not be exposed as Downloaded capability without
verified file state. Application Source/live transport composition is pending.

## Volume leases and guarded file effects

`LinuxGeocachingVolumeLease` reads the existing core volume header and holds a
nonblocking exclusive `flock` for cooperating writers. Paths are injected. It
can initialize only a brand-new `.state` directory with a caller-supplied
nonzero CSPRNG volume ID. Existing/interrupted state, unknown versions and CRC
failures are preserved, never replaced with an empty ledger. Root/state/header
and lock identities are checked; replacing a path invalidates borrowed access.
Release closes descriptors without unlinking the lock file. All access belongs
to one storage owner; acquire/release must not race file effects.

`withFileEffect` holds a SQLite write reservation across a synchronous callback
and requires a matching acquired volume lease. It passes the lease's borrowed
root descriptor, checks the volume before and after the callback, and validates
the active generation and phase. Install requires continuing Prepared intent;
rollback permits cancelled Prepared work; cleanup requires Installed work with
pending cleanup and matching installed metadata. Retained signatures are checked
before file effects. Callback errors/exceptions retain the committed recovery
journal; they do not roll back filesystem changes. Callbacks cannot retain the
Entry/descriptor or reenter database writers.

Native ASan/UBSan tests exercise competing leases, persistent lock-file identity,
corrupt/unknown/incomplete volume state, FIFO rejection, directory replacement,
and kernel lease release after a child writer is killed. A guarded real marker
write blocks another connection's cancellation until the effect ends; unlocked
and wrong-volume calls never execute. Failed callbacks, stale generations,
cancelled rollback, pending/completed cleanup and a root swap during a callback
are checked. Metadata hash values and the marker are test fixtures, not an
installed GPX. Both ownership tests also run as armhf executables under QEMU.

## Native GPX installation file port

`LinuxGeocachingGpxFilePort` now implements the shared `GpxInstallPort` interface.
It combines retained signed records, descriptor-based staging, the install
journal and the acquired volume lease. `GpxInstall` drives the existing check,
stage validation, journal preparation, replacement, installed-file validation
and metadata commit steps. Moving the old file to its task backup returns
Pending; the next call moves staging into place. Every mutation is guarded and
uses a descriptor-relative, no-replace rename on the same filesystem. There is
no unsafe overwrite fallback if the kernel/filesystem lacks that operation.

The final path is `caches/<cache-id>.gpx`. Task files retain the formal staging
layout, including `<task>.old.gpx`. Successful cleanup moves the old canonical
GPX into `.state/history/<old-revision-hash>.gpx`. Existing matching history is
verified before removing a duplicate backup. Unknown/conflicting history keeps
the backup and unfinished cleanup marker. The native task row retains the old
revision reference alongside the core install record so cold cleanup can load
and authenticate that exact version.

Rollback cancels intent before moving an uncommitted new target aside,
restoring the verified old backup, removing known task artifacts and marking
RolledBack. An operation that never prepared a journal never owns a final-cache
mutation; cancelling it preserves pre-existing files. Completed rollback and
cleanup acknowledgements are idempotent, while superseded generations remain
stale. Unknown files are not overwritten or deleted.

The integrated native ASan/UBSan test runs the actual shared install state
machine against real GPX files and SQLite. It covers creation, version update,
history retention, cancellation after replacement, rollback, late callbacks,
terminal retries, cold cleanup and history collisions. A child process is killed
after the old file has been durably backed up and before the new target is
placed; a fresh lease/journal/port resumes and completes installation. The same
test runs as an armhf executable under QEMU. These are software/host-filesystem
checks, not physical power-cut validation on CM0 storage.

This file port is compiled into Linux common infrastructure. The background
source now recovers persisted work and projects authenticated installed copies.
Authenticated network download admission remains unconnected, so the application
must not claim live download support yet.

## Background source and UI lifetime

`LinuxGeocachingSource` now owns the storage thread and a bounded mailbox: one
mutation, one draft read, a coalesced window request and four summaries. A
completed mutation cannot be replaced until its status is read. Input text is
encoded/copied before admission; asynchronous work retains no UI callbacks or
borrowed editor strings. Draft IDs are prepared on the worker. Disk/SQLite
operations run outside the mailbox mutex. A regression holds a SQLite write
lock while requesting a save and verifies snapshots still return promptly.

The facade owns the source and injects the resolved runtime database path and
storage root. UI
composition owns page binding: the SDL main uses a scope guard, and the compact
shell detaches the page before the host destroys LVGL. Headless common-library
users therefore do not acquire a dependency on geocaching page symbols. The
host's teardown cancels page callbacks and unbinds before the worker is stopped.
SDK hosts with externally owned LVGL must call `releaseUiBindings()` before
destroying LVGL.

Native source tests cover copied text, persisted reopen, stale generations,
save/edit/delete, and missing transport capabilities. A real compact-shell/page
test enters Geocaching through the production menu using Right/Enter, navigates
actions with Tab/Enter, types the draft name through the keypad input queue,
inserts/deletes a character in the middle using cursor keys,
exercises My caches, New, Save, reopen, edit and confirmed local delete, returns
to the menu and re-enters. It leaves the page open to exercise host teardown.
The test and rendered 320 x 170 capture establish software navigation and layout;
physical Cardputer keyboard mapping still needs a device.

The cursor regression reproduced a production-page focus bug. Keypad textareas
and dropdowns now consume their own direction keys; Tab continues changing
focus. Encoder users retain the existing group edit/navigation behavior.

The worker acquires an exclusive volume lease, resumes prepared installs and
unfinished cleanup, and exposes a four-row downloaded window. Details are
authenticated and read off the UI thread. The copied projection is invalidated
when a volume changes, a file becomes corrupt, or the worker fails. UI callbacks
never retain borrowed record text or perform file reads.

Offline metadata resides with the volume at
`trailmate/geocaching/.state/native-cache.sqlite3`; local drafts remain in the
application database. A binding records the volume UUID and schema version.
Existing volumes with missing or unrecognized metadata are retained for review,
not reset. Initialization interrupted before establishing the binding likewise
requires review. The source creates its owned subtree only beneath an existing
storage root; it does not create a missing backing mount. Explicit refresh retries
lease acquisition and recovery, while background checks refresh the visible
window and detect changed files.

## Local Map projection

The native source supplies the shared `LocalMapOverlay` with a separate local
data revision. Successful draft mutations and visible offline-copy changes
advance it; pagination and unchanged refreshes do not. This lets a stationary
Map refresh after saving, moving, archiving or deleting a draft without
continually restarting its bounded scan. Draft summaries carry the stored
state so the shared archive filter receives the same data as other platforms.

The map reads at most four metadata rows per requested window, reusing the
existing shared nearest-marker selection and marker limit. It never starts
directory discovery or performs storage I/O. Explicit coordinates at (0, 0)
remain valid; drafts without coordinates do not invent a marker there.

Drafts are held in the application database independently of the offline
volume. `Source::mapSnapshot` defaults to the existing browse snapshot for
other platforms. The native override can omit a known unavailable downloaded
section from Map, while preserving that section's browse error and any busy
loading state. Volume availability changes advance the map revision, allowing
subsequent scans to retry it. Losing the offline mount therefore does not
prevent the shared overlay from reaching readable local drafts.

The native regression drives the actual SQLite source and shared overlay:
multiple pages, archive filtering, rename/coordinate updates, local deletion,
unchanged revisions while paging, stale-write rejection and reopening with a
missing offline volume. It instruments the source, draft store and inline map
projection with ASan/UBSan/leak checks. The existing portable overlay regression
also exercises the default snapshot behavior used by other platforms. These
checks do not establish physical GPS, storage hotplug or map gesture readiness.

The source integration regression runs the actual worker with ASan/UBSan. It
bootstraps a volume, installs a signed revision through the shared file port,
leaves a second revision durably prepared, and reopens the source to recover it.
It checks the downloaded row and detail, invalidates both after GPX corruption,
and confirms that missing backing roots and lost existing-volume metadata are
not recreated. Its shared Map overlay also drops the corrupt installed copy,
while unchanged explicit refreshes preserve the local map revision. After
native Map integration, the full Linux suite has 82 passing tests;
the separate compact-shell suite has 14 passing tests. The rebuilt armhf
package also starts its real mesh worker and renders through SDL under QEMU.

The source and storage port are included in the Linux executable. CM0's existing
geocaching route can use local drafts and read recovered offline copies.
Directory discovery/download and signed
publication remain unavailable with explicit status text. Neither product has
complete geocaching support. Cardputer's compact menu route is now registered.

The native Map/source regression and extended offline-copy regression also pass
as actual armhf executables under QEMU. The rebuilt standard Debian package was
extracted and its real application launched with hardware RF disabled; identity
bootstrap, Agenda/waypoint metadata and the expected 1232 x 568 rendered frame
were checked. The packaged Logo and README match the repository inputs.
No physical peripheral or peer-radio result is implied by these tests.
