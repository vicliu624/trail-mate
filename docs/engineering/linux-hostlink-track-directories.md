# Linux track directory discovery and application transport

Formal operations 89 (directory list) and 90 (discovery request) bind to the
native mesh-worker directory service. The HostLink capability list advertises
them only while a matching database has a registered Reticulum identity owner.
An offline discovery request returns Busy. A successful discovery response means
bounded worker admission, not a directory result or publication confirmation.

## Verified application delivery

`LinuxReticulumLinks` authenticates the LXMF envelope once, then dispatches plain
text or standard custom fields to separate acceptance ports. It checks the local
destination, source/public-key binding, original signature and payload shape.
Custom types are bounded to 64 bytes and data to 8192 bytes. Borrowed callback
views expire at return. The callback must complete durable business acceptance
or the entire ephemeral control exchange before returning true.

Rejected, unknown, wrong-author and exception paths do not generate a successful
application proof. Accepted custom data uses the existing pending receipt/proof
pump for direct packets and resources. Custom outcomes have their own sink and
bounded ID ownership table; directory requests do not become chat sent/delivered
events. Text, call, resource and propagated-text paths retain their existing
owners and share the original authentication and delivery machinery.

The network owner routes custom data through its selected live connection and
verified peer directory. The mesh worker can use its existing Reticulum RF path
when appropriate; hardware/device selection and transmit permission stay in the
existing transport. No interface name, Linux device index, board ID or directory
server address is introduced by this adaptation.

## Directory ownership and negotiation

Verified announcements must bind the `trailmate/track.directory` service name
to the identity and expected LXMF delivery destination. An accepted advertisement
does not establish directory capabilities. Ignored identities are excluded.
Solicited, signature-verified cached path responses can reach a newly attached
consumer without extending cached path freshness. Unsolicited replays do not.

`LinuxTrackDirectories` owns the shared eight-entry directory pool on the mesh
worker. It negotiates capabilities with a fresh random request ID, bounded
deadline and retry backoff. Responses must match the authenticated endpoints,
request ID, public key and advertised incarnation. The shared capability decoder
requires the complete operation mask before marking a directory verified.
Changed incarnations clear that flag. A small cache acknowledges duplicate,
already processed control messages without reinstating old capability state.

The cross-thread registry registers one live protocol owner per process and
binds it to the configured database path. Duplicate registrations fail closed.
Only fixed-size directory copies and an eight-entry, deduplicated discovery
queue cross this seam; network objects, private keys and borrowed frame spans do
not. The original shared list encoder forwards to a read-only view encoder so
the copied metadata uses the same wire layout.

Frontend sessions claim a consumer lease. Other sessions cannot take it over;
matching close, TCP disconnect and 30-second inactivity retire its rows and
pending work. Consumer generations prevent old producer snapshots from
reappearing in a new session. Expiry tolerates a producer timestamp captured
just before a concurrent frontend renewal. Protocol context changes also reset
negotiation state. Already admitted transport frames can finish independently;
their old responses cannot repopulate a new consumer generation.

## Evidence and boundaries

The transport-focused gate passed 19/19, including actual TCP bidirectional
custom traffic, an 8192-byte resource, no-proof rejection paths, verified service
announcements and existing text/call/resource/propagation regressions. The
directory integration gate passed 26/26. The complete native suite passed
263/263, and the compact suite passed 19/19. Both compact runtime targets and ARM
application/audio/hardware tools built.

The native directory fixture covers database ownership, the shared list wire
shape, nonce/incarnation mismatch, duplicate control replies, session conflict,
close/idle/context retirement, bounded queue admission and concurrent renewal
ordering. The formal Node client covers real HostLink session/list/offline
discovery behavior. Independent Python acceptance additionally exercises the
mesh-worker registration and exact native main executable with temporary roots,
dummy SDL and physical radio disabled.

On CM0, directory runtime and authenticated-announcement fixtures, nine custom
delivery cases, real TCP route integration and independent HostLink session
checks passed. This is software/loopback evidence. It does not claim physical RF
directory-server interoperability or directory publication.

The installed standard deb also passed lintian, `dpkg -V`, original-logo
comparison and isolated startup/timed exit. The exact installed executable
passed the independent directory session test in mesh mode with physical radio
disabled, dummy SDL and temporary identity/storage roots. No current user
identity/recording was changed. No screenshot, reboot or display-driver change
was used. The prior signing package remains available as
`trailmate-cm0-before-directories.deb`. ESP stack hygiene, Reticulum scope and
whitespace guards passed. The cumulative GitNexus audit remains CRITICAL:
213 files, 849 symbols and 27 flows, including earlier accumulated changes.
No commit was made.

Operation 83 publication, remote history/confirmation and the remaining owned
track operations are separate work. Local signing and installed route packages
remain separate completed paths. See [local signing](linux-hostlink-track-signing.md)
and [current CM0 acceptance](cm0-adaptation-acceptance.md) for the artifact and
current task boundary.
