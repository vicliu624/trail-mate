# Linux HostLink remote track reads

Operations 91–94 bind remote track reads to the existing native Reticulum worker.
The implementation supports directory capabilities, query pages, signed manifests,
file chunks and the region capability/children/descriptor/boundary/query operations
(network operations 0, 1, 2, 8 and 9–13). These operations are included in the
installed CM0 standard Debian package; the
[acceptance audit](cm0-adaptation-acceptance.md) identifies the current artifact.

## Ownership and protocol reuse

`LinuxTrackRead` owns one `HostlinkTrackReadOwner` on the mesh worker. It pins a
directory through the same verified discovery registry and management-session
binding as publication. The request supplies delivery and epoch selectors, not
an arbitrary trusted directory key. Native identity, transport and storage roots
are injected; the implementation contains no board, interface or device-node
selection logic.

The owner copies the complete bounded USB request before decoding it. All query,
cursor, manifest, file and regional views refer to that stable private copy.
Shared query-binding primitives validate cursor scope, and pinned directory limits
bound ordinary query/file requests. The shared `ReadNetworkExchange`,
`ReadResponseValidator`, `ReadResponseSink` and `ReadResponseStore` provide network
correlation, operation-specific validation and durable response caching. A native
adapter supplies serialized binding visits and the existing authenticated custom
LXMF transport. No response is acknowledged before validation and storage finish.

## Frontend and storage boundaries

Only bounded commands and copied replies cross the database-scoped mailbox.
The dedicated HostLink worker waits up to one second for a mesh-worker visit,
releasing the mailbox mutex while waiting; neither the UI nor router lock is
held. This lets the production client's chunk reads return their data directly.
Remote response waiting remains in the shared network state machine.

Response paths derive only from canonical session/request IDs under the configured
storage root. Admission holds the same exclusive storage lease used by uploads,
signing and publication, and rejects existing response paths. Descriptor-relative
file operations reject symlink traversal. Metadata retains the original response
digest; the browser verifies the complete reconstructed response against it.

Status/chunk/release requests must match the owning session and request. Release
removes only this owner's temporary response files. Disconnect, session close,
idle expiry, author replacement or protocol-context changes retire the owner.
Late replies cannot create a response after retirement. Imported track content
and publication journals are separate from these disposable read caches.

## Verification scope

Fourteen native scenarios cover every supported network operation, wrong source,
forged manifest signatures, disconnect, context changes and timeout. The production
web read-session adapter exercises actual HostLink TCP capabilities, manifest/file
reads, queries, digest checks and automatic cleanup. An independent Python/OpenSSL
client verifies signatures and reads the same protocol through TCP.

The read-peer fixture supplies a directory contract responder and pinned test
directory. It does not establish independent external-directory or RF
interoperability.

The focused gate passed 55/55, the complete native suite passed 317/317, and the
compact suite passed 19/19. The fourteen new scenarios passed on CM0, as did the
independent TCP/OpenSSL signature, file, digest and response-cleanup checks.
The exact installed main application passed capability and unpinned-directory,
stale-identity, invalid-chunk and expired-read rejection checks using a temporary
mesh configuration with physical radio disabled. The standard deb passed Lintian,
installation, `dpkg -V`, original-logo verification and isolated startup/timed exit.
