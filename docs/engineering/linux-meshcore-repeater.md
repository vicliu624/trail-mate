# Linux MeshCore client forwarding

The Linux raw LoRa adapter now honors the existing `meshcore_client_repeat`
setting. The default remains disabled. The wire planner owns no hardware, UI,
persistence or radio lease; the existing mesh worker executes scheduled sends.
There are no board names, device nodes or pin assignments in this integration.

## Wire and scheduling policy

The planner reuses the shared MeshCore packet parser, path descriptors, public
hashes, payload-shape checks, packet signatures and airtime/RX-delay functions.
V1/V2 flood frames append the local public hash and respect the forward-profile
filter, hop limit, path/frame bounds and self-in-path rejection. Local private
destinations and local advertisements are not flooded again. High-bit discovery
controls remain zero-hop exchanges.

Directed transit frames are consumed at the adapter boundary rather than
delivered as local plaintext. Only the addressed next hop can shorten and
forward the path. ACK/multipart ACK forwarding preserves the token and existing
300 ms burst spacing. Canonical Trace forwarding appends quarter-dB SNR bytes
without altering the trace payload. Terminal traces remain local; this does not
implement native trace origination or its application result projection.

The queue has eight owned slots. Full queues reject new work without evicting
admitted work; an ACK burst requires enough capacity for the whole burst.
Duplicate signatures are retained in a 64-entry, 60-second cache, while queued
entries also suppress duplicates throughout their own lifetime. Successful
outgoing MeshCore frames enter the same cache to prevent relay echoes. Forward
delay uses the shared RX score and jitter policy; queued work expires after its
delay plus a bounded airtime-derived grace period. Clock arithmetic covers
unsigned millisecond wrap. No scheduler sleep occurs on the UI thread.

Local protocol replies retain priority over relay traffic. Discovery listening
temporarily defers relay dispatch. Radio/policy changes, disabling TX, protocol
switching and cancellation discard the previous relay queue. Metadata-only
updates do not discard it. Relaying does not create a chat send/delivery result.

## Advertised role and discovery

Enabled forwarding uses the repeater node type in the signed advertisement and
discovery response. Disabled forwarding uses chat type. Discovery filtering
uses the selected role, so repeater-only queries receive the correct response.
Supported-version zero-hop direct/flood controls are accepted; reserved payload
versions and nonzero-hop discovery requests are rejected. Bluetooth identity
import/export and phone protocols remain disabled and are not part of this work.

## Evidence and delivery

Eight focused native MeshCore tests passed, including group/private codecs,
identity, discovery, service binding, key verification, the wire planner and the
actual raw-adapter integration. The planner also passed ASan/UBSan. After the
zero-hop discovery correction, its affected discovery/adapter tests passed
again. Native/ARM applications, auxiliary probes and both compact device targets
rebuilt successfully.

The ARM Release cache previously disabled assertions in several old MeshCore
fixtures. Relevant targets now explicitly use `-UNDEBUG` after `-DNDEBUG`.
All eight assertion-enabled executables passed on CM0 with fake radios and
temporary data roots. Earlier affected ARM fixture results were weaker evidence;
the native Debug results were unaffected. These are not on-air peer tests.

The standard armhf package passed lintian, installation, `dpkg -V`, original-logo
comparison and isolated application startup/timed exit. SHA-256:
`3375ba644ebc9b6d4cdae61022fc0b8b6951c48efec037e422390e44b98da674`.
Installed executable SHA-256:
`d64cf9ef42f2b2a59ee93679a8c83ff67d2d996b5154791087e0622704498f9b`.
The preceding `1595c016...` package is retained as
`trailmate-cm0-before-repeater.deb` in the device validation cache. No screenshots,
display/boot changes, Bluetooth activation or physical RF peer tests were used.
