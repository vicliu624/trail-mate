# Native Reticulum TCP transport

`LinuxReticulumTcp` provides the native TCP bearer for the shared Reticulum
protocol owners. It consumes the shared `ReticulumNetworkConfig` and manages
up to its three TCP client entries. Endpoint identifiers, hostnames, ports and
interface access credentials come from that configuration. Connections use
system routing across available Linux network interfaces.

The port has one serialized protocol-worker owner. Configuration updates are
validated and prepared before existing connections are changed. Unchanged
endpoints retain their connections across configuration reordering. Each new
connection receives a generation; sends require the intended endpoint and
generation, and received frames carry both. Removed or disconnected generations
lose their queued frames. Reconnection cannot replay an old partial write.

Connection establishment uses libcurl's asynchronous multi interface with
`CONNECT_ONLY`; the easy handle remains attached while raw send/receive is in
use. Proxy configuration is disabled for this direct bearer. The runtime
requires libcurl's asynchronous DNS feature, and exposes a bounded connection
timeout. These API lifetime requirements follow the
[libcurl connection-only contract](https://curl.se/libcurl/c/CURLOPT_CONNECT_ONLY.html)
and [raw nonblocking send contract](https://curl.se/libcurl/c/curl_easy_send.html).

TCP framing uses the Reticulum HDLC flag and escape format, followed by the
interface access codec. See the
[upstream TCP interface](https://github.com/markqvist/Reticulum/blob/master/RNS/Interfaces/TCPInterface.py)
for that framing. The native port retains this project's shared 500-byte
Reticulum MTU. It bounds partial input to the MTU plus the largest IFAC tag,
keeps eight transmit slots per endpoint and 32 received frames overall, and
limits read/write work per poll. A full receive queue retires the offending
connection; other endpoints continue. Expired pending writes retire their
connection and discard its queue.

The simultaneous-endpoint regression reproduced stale frames from a retired
connection occupying the receive ring and closing a healthy second connection.
When the ring fills, the owner now compacts out retired generations before
admitting another frame. The wrapped-ring regression confirms that the healthy
endpoint remains online and its queued frame is delivered.

The IFAC algorithm now lives in `InterfaceAccessCodec` under `core_chat`.
It uses the same vendored SHA-256/HMAC and Ed25519 implementation as the existing
ESP code. `IfacCodec` remains the ESP API and enforces its original external-RAM
placement rule before delegating. Arduino's `core_chat` source discovery and the
explicit ESP-IDF source list include the shared implementation. Native Debian
packages include the vendored Crypto license notice.

Four encoding vectors were captured from the original ESP implementation before
extraction, for 1-, 8-, 16- and 64-byte tags. Both the shared implementation and
the ESP wrapper match them and pass round-trip, length, failed-configuration and
public/protected-mode checks. Longer tags additionally undergo per-byte mutation
and incorrect-credential rejection tests. An 8-bit compatibility tag can collide;
the tests do not claim collision-free authentication for it. A synthetic
external-memory port verifies that the ESP wrapper still rejects other memory
placement and clears its previous configuration.

The native socket fixture uses actual IPv4/IPv6 loopback sockets and localhost
DNS. It covers single-byte fragmentation, maximum-size escaped frames, limited
write budgets, IFAC rejection, malformed/oversized input recovery, transactional
configuration rejection, endpoint reordering, partial-write cancellation,
generation isolation, retry, queue limits and descriptor cleanup. It also tests
repeated cancellation during connection setup with sanitizers enabled.

The call fixture now also connects two production TCP ports through a local
byte relay. The endpoints run the production encrypted link handshake, shared
LXST state machine, runtime and Codec2 media owner. The relay forwards bytes;
PCM capture/playback are injected. This verifies the combined digital pipeline
over actual TCP. It does not establish physical acoustic performance or external
peer interoperability.

On 22 October 2026 the final native suite passed 154/154 and Cardputer Zero
passed 19/19. The earlier socket fixture passed ten consecutive instrumented runs,
and the combined call fixture passed five. All five ARM fixtures (IFAC,
TCP, combined calls, call links and delivery links) passed on CM0, including
IPv6 and the simultaneous-endpoint queue isolation regression. The ESP stack
hygiene guard passed. The ESP placement compatibility check uses a synthetic
memory port; an ESP firmware build was not performed for this extraction.

The standard armhf deb, SHA-256
`05b517d44bee7dce825ac58d672084511b9e3216c885838e0fb48b4de6ccc18e`,
was installed on CM0. Lintian emitted no diagnostics; `dpkg -V`, the original
logo hash and the installed Crypto license comparison passed. The installed
P4 homepage and active map passed fullscreen/focus and Alt+F4 exit with code
zero in 0.649 and 0.639 seconds respectively. The previous package is
retained as `trailmate-cm0-before-wifi-profiles.deb` in the device
validation cache.

`LinuxMeshWorker` now owns `LinuxReticulumNetwork` in production. One serialized
owner combines TCP connections, signed delivery/telephony announcements, ingress,
secure links and calls. Each link is pinned to its endpoint and connection
generation; ciphertext arriving through another endpoint is rejected. Retiring
one connection closes only its links and paths. Radio power changes leave a
usable IP call running. Device-local application services select this worker
even when physical LoRa is disabled.

The existing persisted gateway host, port and policy configure an explicit TCP
endpoint when no explicit modern snapshot exists. Linux application services
now persist and reload the full multi-endpoint snapshot with the shared TMS
network codec; see [configuration ownership](linux-reticulum-network-configuration.md).
The optional mesh transport-status port reports cached radio/network readiness
to shared UI without exposing Linux device names. The call capability requires
a connected gateway, transmit permission, non-anonymous identity and media
support. Missing routes request discovery and require a user retry. Asynchronous
dial failures publish a shared runtime fact and a one-shot UI notice.

Eight final ARM fixtures passed on CM0: production worker/application services,
endpoint routing, dial failure, shared call UI, IFAC, TCP, combined calls and
secure links. The worker fixture uses two independent processes, actual TCP and
signed discovery; it verifies rejected versus durable text commits, bidirectional
Codec2, radio-off independence, application-service admission and gateway config
save/reload. PCM is injected. The routing fixture rejects valid ciphertext on
the wrong endpoint and verifies independent connection retirement. The shared
UI fixture also passed in the real Wayland session.

The shared multi-endpoint editor, factory-default policy and native call-safe
configuration deferral are now connected; see
[configuration ownership](linux-reticulum-network-configuration.md).
Factory Reset is also integrated with atomic native settings removal and
quiescent restart; see [factory reset](linux-settings-factory-reset.md).
Native [TMS import/export](linux-tms-transaction-boundary.md) and
[AutoInterface discovery](linux-reticulum-auto.md) are connected and deployed;
LXMF propagation client flows remain incomplete. Known-identity calls now wait
for a signed telephony path with a real cancellable outgoing session and bounded
retry; see [call path waiting](linux-pending-call-path.md). Missing public keys
are bound after verified discovery; a missing identity still requires discovery
before dialing.
External-peer interoperability and physical acoustic performance are unverified.
The shared optional adapter method was built in native, ARM and Cardputer
compositions; a full ESP firmware build was not performed.
