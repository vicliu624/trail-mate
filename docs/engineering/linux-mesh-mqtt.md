# Linux mesh MQTT integration

This integration is incomplete. The shared P4 settings expose Meshtastic and
MeshCore broker enablement, uplink/downlink, host, port, root and credentials.
The ESP standalone MQTT runtime consumes these settings, but the current Linux
application does not yet own the equivalent broker connection or business
bridge. Saving or importing the fields is not evidence that MQTT works.

## Scope and existing boundaries

MQTT is independent of the user's Bluetooth deferral and remains required for
the fitted Linux IP networking capability. In contrast, the current P4/compact
UI has no MeshCore active-Trace entry point: the existing external entry is the
phone facade's MeshCore backend. That phone-facing API remains deferred. The
shared Mono node menu creates only Meshtastic Trace intents. MeshCore radio
forwarding, including canonical Trace relay, remains a separate implemented
data-plane feature; see [client forwarding](linux-meshcore-repeater.md).

The Linux bridge must retain the protocol's existing separation: MeshCore raw
frames use their raw topic, Meshtastic uses service envelopes/channel policy,
and VMP MQTT envelopes remain isolated from both radio packet formats. Broker
input must not accidentally enter the RF forwarding path. Connection acceptance,
completed socket writes and peer delivery are different states.

## Packet codec

The new `chat/infra/mqtt/mqtt311_codec.h` codec has no socket, application facade,
radio, UI, persistence or device-path dependency. Packet and decoder storage is
caller-owned and bounded to a 1024-byte body. Embedded callers must keep these
objects out of task-stack locals. Its client-side broker decoder handles
arbitrary byte fragmentation and coalesced frames, validates fixed flags,
remaining length, UTF-8 topic strings and packet identifiers, and latches errors
until explicitly reset. Returned views expire at the next input byte/reset.

The encoder supports clean-session CONNECT with optional credentials, one
QoS-0 subscription, QoS-0 PUBLISH, PUBACK, PINGREQ and DISCONNECT. The decoder
accepts QoS-0/1 publications and exposes retained/duplicate flags for business
admission. QoS-2 is rejected, matching the current embedded client. Password
fields remain binary; topics and identifiers follow the MQTT UTF-8 contract.
Protocol reference: [OASIS MQTT 3.1.1](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html).

The standalone native fixture passed ASan/UBSan. It checks literal wire vectors,
binary payload/password preservation, Unicode and malformed strings, topic
filters, fragmented/coalesced responses, handshake refusal, QoS-1 identifiers,
remaining-length boundaries and error/reset behavior. It does not establish a
broker session or MT/MC/VMP interoperability.

## Linux client transport

`LinuxMqttClient` now provides a serialized worker-owned connection over the
existing asynchronous curl TCP facility, without a new external MQTT dependency
or wireless-interface assumption. Readiness follows accepted CONNACK/SUBACK;
complete socket writes produce transport outcomes, never peer-delivery claims.
Reconnect, handshake/write/partial-frame/ping deadlines and backoff are bounded.
Invalid updates retain the active endpoint; explicit disable always quiesces it.
The application binding must check configuration admission before publishing.

There are sixteen fixed TX slots with at most eight application publications,
sixteen inbound slots and sixteen reserved publish-result slots. Admission
reserves an outcome for each tagged publication. Disconnect/disable settles
pending tagged sends as unwritten and clears old-generation receive data.
Topic/payload bytes are copied into owned storage. Oversized or unrelated topics
are discarded; QoS-1 acceptance requires buffer capacity before acknowledgement.
Connection generations remain attached to messages and outcomes.

Nine independent Python loopback-wire scenarios passed against the actual client
under ASan/UBSan: fragmented handshake, coalesced publications, QoS-1 PUBACK,
owned binary data, subscription matching, heartbeat/reconnect, generation fencing,
queue saturation/disable outcomes, missing/refused CONNACK, wrong SUBACK ID,
oversized/incomplete/invalid-UTF-8 packets and unsupported QoS-2. These tests use
plain TCP and a wire fixture, not a public broker or full MT/MC/VMP business flow.

## Meshtastic service envelopes

`LinuxMeshtasticMqttCodec` now encodes/decodes the actual service envelope and
retains the complete normalized MeshPacket, rather than converting MQTT data
through a radio MTU. The full legal 233-byte Data payload is supported even when
its envelope exceeds 255 bytes. The existing shared channel/uplink/downlink,
decoded-payload/encryption, admin rejection and gateway/source echo policies are
reused. MQTT-origin packets cannot be republished through this codec.

The bounded decoder reads the packet into owned static-field protobuf storage
without allocating service-envelope pointer fields from network input. It
rejects duplicate known fields, validates topic/channel/gateway agreement and
supports legacy envelopes without a gateway field while still rejecting local
gateway echoes. Unknown protobuf extensions are skipped. PKI ciphertext remains
opaque, and a plaintext PKI claim is rejected; the PKI output flag is a selector
for subsequent cryptographic verification, not proof of authentication.

The native ASan/UBSan fixture passed literal protobuf-vector comparison, maximum
payload, binary data, both configured channels, policy and echo rejection,
extensions/duplicates, legacy gateway handling and retained metadata. Encoding
a reply does not overwrite the codec's RX packet storage. These checks are
envelope evidence, not a completed application receive/send path.

## Protocol-worker bridge

`LinuxMeshMqttBridge` now selects separate MT, MC and VMP receive paths. MT uses
the complete service envelope; MC uses a bounded opaque raw frame, rejecting
retained input and its own publication topic. VMP-topic bytes are handed to a
distinct kind for the VMP owner's validation/authentication; they never pass
through the MT decoder. Topic routing alone is not VMP authenticity evidence.
The bridge has no radio dependency and does not forward broker input to RF.

Its publish contract completes only after a whole socket write. The bounded
wait runs on the protocol owner, checks teardown cancellation, and preserves
RX storage while replies are encoded/sent. A complete configuration change is
a new receive cohort. Bridge generations remain monotonic even when the
underlying client object is replaced. Invalid requested configuration remains
selected but unready, preventing implicit fallback to a different carrier.

The native ASan/UBSan loopback fixture passed MT maximum-payload receive and
literal protobuf reply comparison, MC raw publication/retained-and-echo filtering,
VMP topic separation, owner cancellation and invalid-configuration/cohort checks.
The MT case also passed over IPv6 loopback. The fixture's VMP payload is
intentionally just opaque carrier data; it does not test VMP authentication,
inbox commit or media handling. No real broker or physical radio was involved.

## Application and worker binding

The working application now registers these components in the common Linux
library. `LinuxAppServices::applyMeshConfig` captures the selected protocol's
broker fields together with its mesh configuration. One atomic worker update
owns protocol/endpoint changes, carrier teardown, receive-cohort invalidation,
broker connection and send completion. MQTT remains disabled unless configured.
Invalid selected MQTT configuration is unready rather than an implicit RF
fallback. Reticulum retains its existing independent transport owner.

The raw protocol engine now shares its normalized Meshtastic Data handler between
air input and service envelopes; encrypted MQTT packets still use the existing
PKI/channel decryption before that handler. Plain MQTT input is not relabelled
as encrypted through an artificial radio-packet round trip. Network input has
External provenance and no measured RF signal/frequency facts. MT inbox queues
are bounded. The RF forwarded-MQTT filter remains on the RF ingress; explicit
broker downlink is governed by its own MQTT policy.

MC input reuses its existing signed-advert, private/group and discovery handlers,
with network provenance, without RF forwarding or learned RF return paths.
Standalone MC clients advertise a chat role. Replies and local sends use the
broker completion contract. Radio power control remains separate from network
readiness. VMP's direct-RF recording service receives only physical-radio
readiness, never generic MQTT readiness. A separate MQTT receive entry point
copies envelopes into a fixed 16-slot queue, carrying the current identity and
configuration epoch. The voice worker authenticates private transfers, recovers
media shards and commits attachments locally through `LinuxVoiceStore`; it does
not construct a radio session. Incomplete transfers expire after 30 seconds,
and context invalidation rejects queued frames from the previous epoch. MQTT
outgoing recording selects this carrier explicitly, without an RF fallback.
The voice worker builds an independent MQTT transfer, commits the outgoing
attachment, and publishes each envelope through the serialized mesh worker.
The bounded command owns its payload and completion state. Configuration
invalidation or shutdown releases pending completions; cancellation prevents
queued envelopes from being sent. Only complete writes of every envelope mark
the attachment Sent. This status does not claim remote delivery.

The native `linux_voice_mqtt_inbox` service fixture passes under ASan/UBSan:
public and authenticated private objects, two missing media shards recovered
through parity, duplicate object suppression, receive-disable admission and
byte-identical attachment reopening. Every physical radio operation aborts the
fixture. This directly verifies the voice-service owner, not an end-to-end MQTT
broker-to-inbox run. The existing voice-service and MQTT application fixtures
also pass in the same targeted gate (3/3).

The subsequent `linux_voice_mqtt_worker` fixture connects two independent
voice/mesh-worker processes through the loopback MQTT wire relay. It records
five-second synthetic PCM clips, publishes broadcast and verified private
objects, receives and persists them, and reopens both stores to validate media
availability. Physical radio operations abort the fixture; the injected port
does not even advertise an FSK capability. This fixture and the inbox, existing
voice-service and MT/MC application tests passed together (4/4). Audio input is
synthetic, so this does not establish physical microphone performance.

Applications without an assigned foreground port use a separate unavailable
radio port for network voice. They never borrow an injected mesh port or an
unrelated global foreground client. Physical recording admission still checks
the foreground port's FSK capability. Self-originated MQTT control echoes are
discarded before they can replace an inbound voice transfer.

The application fixture runs two separate service/worker/chat-store processes
for each of MT and MC over a local wire relay. It forbids physical acquisition,
configuration, RX, TX and RSSI methods. Broadcast, NodeInfo/key learning, encrypted
private chat, send status and External inbox provenance passed and survived
application-service reopening. Related existing worker/radio/group/private-codec
regressions also passed. These tests are actual application-service evidence,
not only packet-codec tests; they still do not exercise the P4 form visually or
an independent production broker.

The private test exposed a pre-existing OpenSSL CCM encryption failure: the
native path requested an eight-byte tag without first configuring its length.
The OpenSSL-only fix follows the [CCM tag-length contract](https://docs.openssl.org/3.2/man3/EVP_EncryptInit/#ccm-mode).
A minimal fixture failed before the fix and passed afterward against independent
Python cryptography vectors for 128/256-bit keys and 8/16-byte tags, including
AAD/no-AAD and altered-tag/nonce refusal. No Arduino crypto branch was changed.

Independent Mosquitto 2.0.18 interoperability also passed for the MT application
pair, MC application pair, and VMP voice/worker pair. The broker was extracted
into a local test cache and bound only to IPv4 loopback, without installing a
system service. A test-only file barrier ensures both processes have subscribed
before either publishes. These runs cover a real production broker with ordinary
MQTT 3.1.1 TCP sessions; they do not establish broker TLS interoperability.

The ARM build and four groups on the actual CM0 passed: voice inbox, independent
PKI vectors, MT/MC application pairs, and the VMP recorded-media worker pair.
The complete native run passed 216/217. Its TMS lifecycle fixture depended on
real audio admission despite testing a hardware-independent settings/call gate;
after explicitly isolating its realtime hooks, that test passed separately.
The rebuilt Cardputer Zero targets and compact suite passed (19/19).

A previously installed standard `trailmate-cm0_1.0.0_armhf.deb` includes these changes:
SHA-256 `8ea6b4213c1359338fac1c53f75b77ffad5cfcf7295f143d0c6b4c05018baf53`.
The [CM0 acceptance audit](cm0-adaptation-acceptance.md) records the current artifact.
Lintian, installation, `dpkg -V`, the original logo hash and isolated installed
application startup/timed exit passed. This subsequent package includes
MeshCore NodeInfo query/reply and finite RF-measurement projection. Distinct
short/long names, MQTT provenance, absent RF measurements and persisted key/name
state passed in actual application pairs on both the host and CM0, with the host
Mosquitto run also passing. The previous `589bef8d...` package is retained as
`trailmate-cm0-before-nodeinfo.deb`. The later identity-management release also
retains `2d6836dd...` as `trailmate-cm0-before-identity.deb` and passed 222/222
native and 19/19 compact tests. No public broker, physical RF,
screenshots, reboot or display-driver changes were used for these release gates.
