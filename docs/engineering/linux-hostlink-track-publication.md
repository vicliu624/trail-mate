# Linux HostLink track publication

The native implementation connects operation 83 (commit) and track jobs through
48 (get) and 50 (cancel) to the mesh worker. These operations are included in the
installed CM0 Debian artifact. See the
[acceptance audit](cm0-adaptation-acceptance.md) for the installed release evidence.

## Ownership and authority

The HostLink frontend copies bounded requests into a database-scoped mailbox.
The existing mesh worker owns publication execution and uses its current author
identity, transport and protocol context. Directory selection must resolve to a
verified directory entry pinned by the requesting management session. Device
addresses and interface names are not part of the publication implementation.

An admitted job holds the captured storage root and exclusive storage lease.
Admission rechecks the retained signed manifest, author generation, object,
revision and directory key binding using shared track domain predicates. Global
operation lookup and admission transfer the same storage lease, preventing a
second process from reusing the operation during the handoff.

Local durable admission does not mean remote publication. The shared network
session uploads content and requests publication; only a valid directory-signed
receipt for the exact job can establish Confirmed. V2 publication uploads the
checked attachment index and required resource blobs before the KML publication
session. Upload restart recovers remote offsets by content hash.

## Recovery and cancellation

Job lookup scans canonical publication slots beneath the captured object root
using directory descriptors. It rejects symlinks and authenticates candidate
journals against retained manifests and signed receipts. A worker restart can
therefore report a confirmed job without sending another publication request.
Identity changes invalidate cached results and execution owners.

Closing a management connection does not cancel an admitted publication.
Protocol suspension pauses execution. Cancellation uses the shared pre-publish
transition under the storage lease; a job already Publishing or Confirmed cannot
be reported as cancelled. Historical cancellation authenticates the same durable
bindings before changing a journal.

## Verification scope

Native tests exercise genuine local signing, durable journals and Ed25519
receipts with a directory contract responder. Cases include V1 and V2 uploads,
lost replies, restart, required-resource corruption, invalid receipts, source
binding, detached management sessions and cancellation. These tests do not
establish interoperability with an independent directory server or an on-air
LoRa peer.

The complete native suite passed 291/291 and the compact suite passed 19/19.
The production ARM application and all three publication fixtures built using
the CM0 target ABI. On CM0, seven admission, fourteen execution and seven service
scenarios passed. The standard Debian package passed Lintian, installation,
`dpkg -V`, original-logo verification and isolated startup/timed exit. Tests used
temporary identities and storage, leaving the user's identity untouched. The
current package hash is recorded in the acceptance audit.
