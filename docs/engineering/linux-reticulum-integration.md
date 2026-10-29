# Native Linux Reticulum integration

## Scope and ownership

The native mesh worker owns a persistent Reticulum identity component and
verified announce ingress, and publishes `ReticulumLocalIdentityInfo` plus RX
diagnostics through its existing mesh port. It verifies incoming announcement
bindings and learns bounded paths through the shared protocol owners. Directory
path requests now run through its bounded command queue. Verified person
destinations enter the shared registry and native contact projection. Incoming
delivery-link requests now use the shared session owner and native cryptography
for handshake/control processing. Authenticated plain link text now reaches
the existing application/store commit boundary and receives a packet proof
after durable acceptance. Peer-text sending uses the existing destination port,
path discovery, link establishment and signed receipt tracking. Encrypted
unsplit peer-text resources also use that durable commit boundary, with bounded
bzip2 expansion and resource completion proofs. Split resources, metadata,
custom fields, group/opportunistic messaging, propagation, LXST and authenticated
Geocaching transport remain unfinished. Peer-text capability is advertised;
readiness still requires the identity and radio to be available.

The runtime follows the ownership freeze and
[Reticulum client architecture](../reticulum_client_architecture.md). Identity
facts have one worker owner. UI callers receive public copied metadata, issue
configuration intent and never open the identity database or perform crypto.
The ESP adapter is not copied into the native platform. Path management, packet
routing, link/session and resource state, runtime allocation and handshake hash/
signalling calculations now have shared canonical implementations. Existing ESP
include paths forward to the canonical headers. Delivery attempt receipts also
have a shared canonical owner. Remaining resource/propagation workflows still
need native composition.

The native store implements the existing durable append and incoming-hash
ports used by protocol acceptance and application commits. See
[Native chat durability](linux-chat-durability.md) for transaction, migration,
deduplication and failure/retry semantics, and
[native link-text reception](linux-lxmf-incoming.md) for the connected worker,
signature verification and proof boundary. See
[native outgoing peer text](linux-lxmf-outgoing.md) for sender/outcome semantics.

The native link owner also exposes outbound delivery-link establishment using
an already verified full public key. It recomputes the destination binding,
uses fresh encryption/signing keys, supports a next-hop header for learned
multi-hop paths, and validates the responder proof against the known identity.
The derived token key authenticates RTT confirmation and identity declaration.
Pending opens coalesce by destination; failed physical requests release their
sessions. The native destination-text queue now uses this API. Two independent native owners
exercise the complete request/proof/RTT/identify exchange in the regression.

## Incoming link control

`LinuxReticulumLinks` is owned by the existing mesh worker. It accepts requests
only for the local delivery destination, checks transport/hop/MTU/mode bounds,
and keeps at most eight sessions without evicting a live session to admit an
untrusted request. It generates fresh X25519 ephemeral keys, rejects invalid
shared secrets, derives the token key with the canonical HKDF, and signs the
response using the persistent Reticulum identity. A repeated pending request
uses its existing ephemeral session. Handshake timeout, radio reconfiguration,
shutdown and failure clear session secrets before releasing state.

An incoming encrypted RTT confirmation must authenticate before a session
becomes active. Encrypted identity declarations verify the signature over the
link ID and public identity. This does not grant contact trust. Keepalive and
authenticated close controls are handled; unimplemented application/resource
payloads do not receive delivery proofs. Native timeout policy is currently a
15-second handshake and 45 seconds without accepted inbound activity; periodic
initiator keepalives now use the shared lifecycle predicate in the native sender.

All responses use the existing RNode carrier and radio coordinator. The worker
checks the desired configuration generation, protocol, TX enable and interface
policy before response transmission. UI ports exchange copied messages/metadata
and queued commit intent. A
successful control handshake does not enable message sends or mark a message
Delivered. The native regression checks signature/key agreement, authenticated
RTT, tamper rejection, identity/close controls, invalid mode and low-order keys,
bounded capacity, timeout/failure cleanup, and real worker/carrier binding.

ARM Release tests explicitly retain assertions for the new link test and the
existing mesh-worker test. Earlier worker runs with assertions disabled cannot
establish the assertion-based behavior; current ARM verification must use the
corrected targets.

## Persistent identity

`LinuxReticulumIdentity` receives its database path from composition. Its native
private encoding is the 32 raw X25519 private bytes followed by the 32 Ed25519
seed bytes. The public key is the concatenation of the corresponding 32-byte
public keys. MeshCore and Meshtastic keys are not reused.

Creation uses OpenSSL CSPRNG output and one SQLite immediate transaction with
full synchronous durability. The component publishes no identity before COMMIT.
Concurrent first openers converge on that committed row. A schema version and
positive author generation accompany the key pair. Existing rows are checked
for column types, lengths, supported version/generation, unique singleton shape
and the derived public/private relationship. Missing or damaged rows refuse
initialization without generating replacement keys. The component has no reset,
restore or private-export API; these workflows require their existing product
authorization and generation-owner contracts before being exposed.

Linux opens only a user-owned regular database file without following symlinks
and restricts its permissions to 0600 before storing identity material. Other
application tables remain intact. The connection's busy wait is bounded and
belongs to the worker. Private scratch and retained private bytes are cleansed
on release. Signing and shared-secret operations use loaded keys without
reopening storage, reject output aliases into the identity object and clear
failed outputs. X25519 rejects zero shared secrets using a constant-time check.
Builds without OpenSSL return unavailable and do not create an identity file.

Identity and destination hashes reuse the shared Reticulum wire helpers. The
worker caches the full identity hash and derived LXMF delivery/propagation
addresses; deriving an address does not register a propagation service. Node ID
is only the shared 32-bit projection; the full hashes remain authoritative.

## Worker binding

Selecting Reticulum initializes its identity on the existing mesh worker.
Configuration changes invalidate old public snapshots until the matching
generation is published. `getReticulumLocalIdentityInfo` copies that snapshot
under the mailbox lock and supplies current name/privacy metadata. It returns
unavailable for another selected protocol or a stopped worker. A blocked
identity transaction does not hold the UI mailbox mutex. Worker restart reloads
the same identity and address.

The worker distinguishes identity initialization failure from an unavailable
transport in its status. Valid identity metadata never changes unsupported send
results to success, grants RF readiness, emits an announce or implies delivery.
An initialization failure is retried on the next configuration application or
worker restart. Periodic retry/backoff still belongs in the pending native
transport lifecycle integration; it is not implemented by this identity stage.

## Shared path and announce ingress

The canonical `lxmf_runtime_state`, `lxmf_memory`, `lxmf_path_manager`,
`lxmf_packet_router` and `lxmf_transport_runtime` files live under `core_chat`.
Their ESP header locations remain forwarding compatibility entries. The
generic allocator lives in `core_sys/platform/memory`; the old ESP allocator
name aliases that same type. ESP still requests SPIRAM plus 8-bit capability
without internal-heap fallback. Host allocation retains malloc/free behavior.
State layouts retain the existing raw/expanded key sizes without including an
ESP identity implementation in shared headers. Build manifests reference the
single canonical implementation, including ESP-IDF and the conformance target.

`AnnounceVerifier` owns the common packet-shape, identity/destination binding,
signed-byte ordering and signature-port invocation. ESP's existing ingestor and
Linux use it. Its 500-byte member scratch exactly replaces the embedded
ingestor's former scratch budget. Platform Ed25519 implementations stay behind
the verifier callback; the wire format and signature domain are unchanged.

`LinuxReticulumIngress` runs on the mesh worker. The raw radio adapter delivers
reassembled Reticulum frames synchronously to its borrowed sink, which is detached before
the ingress context is destroyed. Other selected protocols do not feed it.
Valid announcements pass the shared verifier and replay/staleness decision
before entering the existing `PathManager`. Ingress increments hop count as in
the existing client, respects the hop limit, ignores local identity announces,
retains header-2 next-hop/interface metadata and culls bounded TTL caches.
Radio/protocol reconfiguration clears this transient context. Interface IDs are
logical handles supplied through options, not Linux device-node indices.

The integrated LoRa carrier uses the same shared RNode air encoder and
reassembler as the embedded client. Each physical packet is at most 255 bytes;
the one-byte RNode header leaves 254 payload bytes, so a 500-byte Reticulum
packet requires two transmissions. Both transmissions must succeed before the
control send succeeds. Only complete reassemblies reach protocol validation.
Pending fragments are discarded on radio suspension or radio/protocol settings
changes. The protocol MTU remains 500 bytes; physical packet storage is not
expanded. Reticulum radio settings use the embedded client's sync word, CRC
and bitrate-derived preamble, independently of MeshCore settings. Configured
frequency, bandwidth, spreading factor, coding rate and power remain inputs.

The UI receives copied path/accept/reject/replay counters. Learned paths do not
create trusted contacts, grant manual verification, emit an announce, forward
traffic, establish a link or acknowledge a message. Message readiness and message-send
capabilities remain unavailable. No new competing path or message ledger is
introduced.

## Verified person registry and contact projection

DestinationRegistry, PeerDirectoryService and the verified-announcement
projection now have canonical implementations under `core_chat`, with ESP
compatibility headers forwarding to those owners. The common projection keeps
the existing aspect/name/ratchet rules: LXMF delivery and LXST telephony can
represent people; Nomad, propagation, legacy call.audio and unknown service
announces do not become contacts. Telephony maps to the same identity's derived
LXMF delivery destination. The native hot registry is bounded and evicts cold
entries without deleting the persistent book.

The native ingress invokes this internal stage only after signature/destination
verification and path admission. Public keys, observed name and ratchet state
are recorded through PeerDirectoryService into the native directory owner.
Fresh observations never grant manual trust, favorite state or a local alias.
Existing local flags and aliases survive the shared core's fact merge, and an
ignored identity does not publish a nearby-node wake event. A failed UI sink
does not reverse a stored observation or stop the receiver.

`LinuxPeerDirectory` makes this access safe across protocol and UI threads and
moves physical persistence off their memory-operation lock. See
[Native peer directory ownership](linux-peer-directory.md) for acknowledgement,
barrier and failure semantics. The native file publication synchronizes a new
file and directory around atomic replacement rather than removing the old
book first.

The worker publishes copied node metadata carrying the complete Reticulum
identity; the contact service consumes the same shared directory. Protocol
selection now updates both chat and contact partitions. Reticulum node updates
use their full destination hash instead of a potentially colliding 32-bit node
projection. A zero projected node ID does not hide a valid full-hash person.

Regressions sign delivery/telephony and service announcements, preserve ignored,
trusted, favorite and alias policy, test hot-cache eviction and reopening,
exercise a throwing UI sink and reject tampering. Synthetic contact-domain
fixtures test colliding/zero node projections separately from crypto proof
tests. The fake-radio application-service test feeds an external delivery
announcement through polling, registry admission, directory storage and actual
contact queries while message readiness stays false.

## Active directory path requests

The shared mesh facade exposes the optional `requestDirectoryPath` intent.
Unsupported adapters retain the default false result; the ESP product adapter's
existing same-signature method implements it as well as the legacy track port.
The native worker admits a bounded command only for the current Reticulum
configuration, a ready identity/radio projection and enabled TX. Duplicate queued
targets coalesce. The return value acknowledges local admission, not physical
transmission, a discovered path or message delivery.

The native control effect currently uses the integrated LoRa interface. Both
admission and the effect respect the legacy LoRa-enabled flag and interface
policy. Wi-Fi-only, disabled LoRa and unknown policies do not fall back to RF;
the raw owner suspends its LoRa lease and prevents receive/reacquisition under
those settings. TCP/Auto transport and IFAC configuration still require their
pending native interface composition before those features can be exposed.

`path_request_wire` encodes the common 51-byte Header-1 Plain Data control packet
with the existing path-request destination and destination/tag payload. Both ESP
send paths and Linux use this serializer. The owner supplies a fresh random tag;
native production uses OpenSSL CSPRNG without a weaker fallback.

The path owner applies the existing pending-path manager's per-target cooldown,
bounded capacity and timeout. A pending request is recorded only after the
synchronous transmitter reports acceptance; failure can be retried immediately.
The effect runs through the existing raw-radio owner/coordinator, rechecks the
configuration generation and TX permission, and never holds the UI mailbox
mutex during radio I/O. Protocol/radio changes clear transient path requests.

A scoped PathResponse always passes signature/destination verification. A
replayed cached announcement can resolve a matching live request without
replacing the cached route or extending its freshness. Unsolicited replay and
tampering retain the rejection rules. New valid announcements continue resolving
matching requests through the shared path manager. Diagnostics distinguish
accepted physical requests, transmit errors and explicit path responses. No
message-ledger entry or delivery acknowledgement is created for these controls.

Wire tests compare exact header/control/payload bytes and reject malformed
arguments or insufficient capacity. Path-owner tests cover cooldown, failed-send
retry, request expiry, capacity, zero/rolling monotonic time, and authenticated
cached responses without freshness renewal. The fake-radio worker test calls
through `IMeshAdapter`, blocks transmission while reading UI snapshots, observes
the encoded control frame, handles a response, retries a failed transmission
with a fresh tag, and checks TX-disabled/cross-protocol rejection.

The new regression uses all four checked-in external announce fixtures (direct,
rebroadcast and ratchet forms), including the fixture's secondary-reference
status. It also signs independent local test announcements to exercise replay
history after packet-filter expiry, newer/stale updates, tampering, local
identity exclusion, hop limits, path expiration, bounded capacity and reset.
The fake-radio worker test delivers a fixture through the actual radio polling
path and checks the UI diagnostics while send readiness remains false.
The portable announce-vector regression now compares the production verifier
with its independent reference check and exercises modified signed fields.

The ESP allocator contract is tested with a host fake for heap capabilities,
failure without fallback, matching free and size overflow. The gateway suite's
IFAC vector reader accepts CRLF line endings; its reference bytes are unchanged.
Actual P4 cross-compiler syntax checks cover the three moved implementations,
embedded announce ingestor and new verifier using the installed SDK/config.
These are syntax-only checks, not a firmware link or upload.

## Identity verification

The native identity regression checks fresh persistence, reopening, public-key
binding, independent peers, failed-output clearing, output alias rejection,
future metadata, missing rows, concurrent first openers, writer contention,
permission tightening and rejection of non-durable storage. A separate target
compiles without OpenSSL and checks unavailable behavior.

The crypto assertions use the published X25519 vector from
[RFC 7748 section 6.1](https://www.rfc-editor.org/rfc/rfc7748.txt) and Ed25519
test 1 from [RFC 8032 section 7.1](https://www.rfc-editor.org/rfc/rfc8032.txt).
Combined identity/delivery hash expectations were calculated independently with
Python hashlib from those public fixture keys. These are primitive/encoding
checks, not Reticulum packet or peer interoperability claims. The enabled
identity implementation runs under ASan/UBSan/leak checks on native Linux.

The mesh-worker regression holds an identity database writer while reading the
UI port, switches protocols, checks unsupported sends and verifies stable
identity metadata across shutdown/restart. Its fake radio continues detecting
hardware access from the UI thread. Physical RF and peer-device tests are not
part of this stage.

The identity-stage native suite passed 84/84 tests and the independent compact shell
passed 14/14. Enabled identity, unavailable-backend and extended mesh-worker
regressions also passed as actual armhf executables under QEMU. The rebuilt
Debian package was extracted; its application bootstrapped the existing real
mesh worker and rendered 1232 x 568 with hardware RF disabled. Packaged Logo
and README files match the repository inputs. These checks do not establish
Reticulum network interoperability or physical peripheral readiness.

After shared-core extraction and ingress binding, the native suite passed
86/86 and Cardputer passed 14/14. The portable runtime/announce checks passed
2/2 and gateway checks passed 11/11. Native ingress and shared path/verifier
units run with ASan/UBSan/leak checks. Actual armhf ingress, allocator-contract
and extended mesh-worker tests also passed under QEMU. The standard Debian
package was rebuilt and its extracted executable passed identity/store
bootstrap and 1232 x 568 rendering with RF disabled; Logo and README match the
repository inputs. P4's installed cross compiler accepted the five affected
translation units with syntax-only checks. No complete firmware link, upload
or peer/on-air result is implied.

The active-path stage passed the full native suite (87/87), compact suite
(14/14), and actual armhf wire, path-owner and mesh-worker tests under QEMU.
The worker regression also verifies that disabled LoRa and Wi-Fi-only policy
release the radio and produce no extra transmissions. Eight affected P4
translation units, including both existing adapter layers and the shared
serializer, passed actual cross-compiler syntax checks. The rebuilt standard
Debian package passed extracted-program bootstrap/rendering and resource
comparisons. These software checks do not establish peer/on-air path discovery.

The peer-projection stage passed 89/89 native tests and 14/14 compact tests.
Actual armhf directory-concurrency, peer classification and application-worker
regressions passed under QEMU, including the radio-to-contact query path.
Thirteen relevant P4 translation units passed installed cross-compiler syntax
checks. The rebuilt standard Debian package passed extracted-program startup,
identity/store bootstrap, 1232 x 568 rendering and Logo/README comparisons.
These checks do not establish physical radio interoperability, firmware linking
or the still-unimplemented LXMF/link/resource workflows.

The RNode carrier correction passed 89/89 native tests and 14/14 compact tests.
The extended worker regression exercises 254-byte single packets, 500-byte
two-packet sends/reassembly, first-transmit failure, MTU rejection, fragment
invalidation on radio reconfiguration, and signal metadata. Application-worker
announce/path/contact tests now inject actual RNode air envelopes and decode
transmitted envelopes before checking protocol fields. Actual armhf directory,
peer and extended worker regressions passed under QEMU. The rebuilt Debian
package passed extracted-program bootstrap/rendering and Logo/README checks.
This corrects a carrier mismatch with the existing embedded implementation;
physical peer interoperability and LXMF/link/resource workflows remain unproven
or unimplemented as described above.

The incoming-link control stage passed 90/90 native tests, including ASan/UBSan
and leak checks for the native link controller and canonical session/resource
owners, and 14/14 compact tests. The portable runtime-state contract passed
against the relocated canonical sources. Seventeen relevant P4 translation
units passed installed cross-compiler syntax checks. Actual armhf link,
directory, peer and worker regressions passed under QEMU with assertions
explicitly enabled for the link and worker targets. The rebuilt Debian package
passed extracted-application bootstrap/rendering and Logo/README comparisons.
No physical peer test, complete firmware link/upload, outbound link or LXMF
message-delivery result is implied.

The outbound handshake-owner stage passed the same 90-test native suite and
14-test compact suite. Its extended link regression runs two independent native
owners through request, proof, encrypted RTT and identity declaration, including
tampered proof, duplicate pending open, physical TX failure and destination/key
binding rejection. The link test and mesh-worker test passed as assertion-enabled
armhf executables under QEMU. The updated Debian package passed extracted
application bootstrap/rendering and resource comparisons. This proves the link
owner API, not application LXMF queue integration or peer/on-air interoperability.
