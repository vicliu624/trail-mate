# Linux HostLink public route-package installation

The native worker now implements TrackPackageInstall (100) and
TrackPackageInstallStatus (101). These operations import an existing original
signed route package; they do not sign a new owned track, create an owned head,
or assert directory publication. Capability query lists the operations only when
the native crypto-backed coordinator is present.

## Input proof and ownership

Installation consumes the shared eight-field prepare request, binding operation,
KML upload, signed-manifest upload, local device generation, optional attachment
mapping, expected object and expected revision. The local actor/generation is
reread on admission and each background batch. Only the same session's sealed,
verified matching upload owner can be retired for installation. Unrelated,
receiving or failed upload owners remain Busy. A cross-process upload lease
protects the immutable input transaction.

`HostlinkPackageInput` uses the shared bound-upload loader, signed package file
check, verified structured reader, sealed attachment resolver, attachment package
check and KML manifest file check. It verifies the original remote author's
Ed25519 signature and derived object/revision, rather than substituting the local
device author. Version-1 KML-only and version-2 attachment packages are supported.
Every supplied index/resource is bound to current local upload journals and the
signed content. Missing required resources fail; optional omissions are explicit.
Candidate mappings remain inaccessible until the complete package proof succeeds.

## Retention and publication

The coordinator first retains original signed bytes, KML, index and all available
resources in the revision-addressed public cache. Paths derive only from revision
and resource hashes. Shared immutable-copy checks validate existing destinations
and partial prefixes, sync data, verify hashes and read back final files. Existing
different/corrupt destinations fail rather than being overwritten. Limits match
the formal public cache: 64 revision entries, 134 files and 64 MiB plus metadata
per revision. Unknown and partial files consume quota.

Only after retention succeeds does the shared route publisher commit the `.rpk`
binding and then publish the `.kml` as the final visible step. The native publish
primitive uses no-replace rename, with a no-replace link fallback, and directory
sync. Route admission bounds root files to 256 and 32 MiB plus metadata; known
native asset/navigation directories are separate. Installed names are immutable:
an existing different route is never updated in place. Restarted copies recheck
content; completed operations can replay their transaction result. Matching
existing files are not replaced.

The install worker and UI deletion share the core route lease. Native deletion
also takes the same filesystem lease, covering other application instances.
Native route enumeration now matches the formal KML-only contract, ignoring
bindings, temporary files, unrelated assets and symlink entries. Deletion accepts
the configured absolute path supplied by shared pages or a direct filename,
while rejecting metadata, links, traversal and paths outside the route root.
Cancellation, disconnect, close, media replacement and the 30-second idle deadline
release transient work without deleting partial immutable files. A matching retry
can resume after revalidating its inputs. The worker performs bounded background
batches and returns pending state while work remains.

## Fresh status queries

Installation replies use the formal five-field result: operation, state,
object/null, revision/null and path/null. State zero is pending; one is installed.
The same install operation can return its completed transaction result on retry.

Status 101 is a fresh disk proof, independent of upload IDs or cached success.
`StoredRoutePackage` rereads binding, original signature, derived identity, complete
index/resources and actual KML hash/geometry. State two distinguishes a missing
route from installed content; corrupt proof or bytes return an error. A completed
query result is delivered once, and the next query starts another disk proof.
Changing local identity does not rewrite the original public author, while the
query request must still match the current local generation.

## Verification

The input fixture uses real original-author signatures and actual native upload
journals. It checks both manifest versions, wrong revisions/signatures, unsealed
input, required missing resources, resource tampering, wrong KML geometry and
identity change. The installation fixture includes a padded JPEG to exercise
multi-batch work, then covers retention, record-before-route visibility,
idempotent install, fresh queries, restart recovery, missing-route detection,
immutable corruption rejection, cancellation, expiry, identity changes,
UI-delete exclusion, quota checks and no-replace publication. It also builds
image sources/BMP caches through the shared route-storage API after installation.

The optional production-client test uses unchanged `installDeviceTrackPackage`
and `queryInstalledDevicePackage`, including their uploads and attachment plan,
against the real native TCP worker. It verifies successful installation/query
and exact installed KML bytes. This is client protocol/crypto integration, not
physical Web Serial/USB or a physical touchscreen route-preview test.

The installed P4 application was also exercised with an isolated public fixture
exported by the production-client installation test. Tracker listed the package
as one route, opened Route Preview at the signed point, prepared the image cache
to 1/1, and deleted its canonical KML via the UI while preserving the original
signed cache and binding. No map tiles were supplied for the fixture location;
the Missing OSM display was expected. Physical touchscreen/key-matrix operation
and image-strip keyboard interaction remain unverified.

The input, install/query/image-cache and existing image/upload/management/protocol
regressions passed as armhf executables on CM0. On 22 October 2026, the opted-in
native suite passed 122/122 tests; Cardputer Zero Wayland/framebuffer builds and
14/14 tests passed. Platform boundary and ESP stack hygiene checks passed. The
no-OpenSSL adapters passed a C++17 syntax check and do not advertise installation.
The full padded-image install/query/recovery fixture was additionally rerun with
its temporary root on the device's confirmed microSD ext4 root filesystem. It
passed in 18.937 seconds. That duration covers the whole fixture, including
uploads, repeated installs/queries, image cache, rejection cases and deletion;
it is not a worst-case package throughput measurement.

The final standard installed CM0 development deb SHA-256 is
`fa6dd79eedd1d63de2464dd9b3d83654dab5150ede95059016853a9691857c86`.
It passed Lintian without diagnostics, installation and `dpkg -V`. The original
logo and 1232 by 568 P4 render were confirmed. Compositor-confirmed focused
fullscreen Alt+F4 exited with code zero in 0.982 seconds. No kernel, display
driver, boot configuration or network-interface configuration was changed and
no reboot was performed.

Sudden power removal, worst-case 64 MiB media timing and physical USB remain
unverified. The original-author proof establishes integrity, not user trust in
an author. Broader configuration, identity restoration and owned-track
signing/history/network publication are separate unsupported management surfaces.
