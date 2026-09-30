# Reticulum Network Configuration

Trail Mate stores its Reticulum interface and LXMF propagation client setup in
the canonical TMS configuration:

```text
/trailmate/config.tms
```

An explicit TMS network configuration takes precedence over factory defaults,
including entries the user has removed or disabled. The older
`/trailmate/reticulum/config.json` is a one-time migration input, not the normal
runtime configuration. The JSON example below describes that legacy format.

## Built-in TCP entries and device editing

The device retains its limit of three TCP entries. When IP interfaces are
allowed and no explicit network configuration or legacy custom gateway exists,
factory defaults add these enabled entries on port 4242, in this order:

1. `sydney.reticulum.au`
2. `node.reticulumnet.nl`
3. `rmap.world`

In **Settings → Reticulum**, select **TCP Entry** 1, 2 or 3,
then edit **Gateway Host** and **Gateway Port**. These fields read the active
network snapshot and changes use the existing TMS save path. Clearing the host
removes that entry and shifts subsequent TCP entries forward. Add entries in
order, starting with the first empty slot. LoRa, AutoInterface, propagation
settings and the other TCP entries are preserved. An explicit empty TCP list
stays empty after reload. A pre-existing custom legacy gateway remains the sole
TCP default during migration; factory seeds do not replace it. LoRa-only policy
does not add the public defaults.

**Restore Reticulum TCP defaults** explicitly replaces the TCP entries with the
three enabled factory entries, while preserving LoRa, AutoInterface, identity
and propagation settings. It uses the existing TMS save path. TCP fields and
the restore action remain visible on Wi-Fi-capable devices regardless of the
selected chat protocol or bearer; editing these entries does not itself change
the bearer policy. Switching chat protocols reloads settings before rebuilding
the list so previously hidden fields do not show stale `Not set` values.
Reticulum has its own Settings category. Bearer preferences, Auto Wi-Fi and
privacy preferences can be configured without changing the active chat protocol.
The Mesh protocol selector continues to control chat, Contacts and the integrated
radio. Runtime identity fields are shown when the Reticulum chat adapter is active;
opening Settings does not start a network service just to populate them.

The ESP settings-model snapshot and action sink use this same active network
configuration. Their `rt_tcp_slot` choice selects the entry addressed by
`rt_wifi_host` and `rt_wifi_port`; ports accept explicit values from 1 to 65535.
The bounded radio section still fits its existing 12-option capacity, without
enlarging the settings snapshot. Direct action clients may also address a slot
with `rt_tcp_1_host` / `rt_tcp_1_port` through `rt_tcp_3_host` /
`rt_tcp_3_port`. These actions preserve other interfaces and request the normal
TMS configuration save. Slot selection itself is transient UI state.

### Selection evidence (2026-09-24)

All three defaults use domains rather than a bare IP. The
[RMAP discovery list](https://rmap.world/nodes.php) showed approximately 173 days
since first observation for Sydney (3,883 announces), 185 days for RMAP's RNS
transport (854 announces), and 125 days for ReticulumNet NL (480 announces),
with recent announces. These are observation ages and counts, **not continuous
uptime percentages**. [RMAP](https://rmap.world/info.html) and
[ReticulumNet NL](https://www.reticulumnet.nl/en/get-started/) explicitly publish
these public connection endpoints. Sydney is also listed by
[directory.rns.recipes](https://directory.rns.recipes/).

The available evidence supports choosing established, recently observed public
nodes; it does not prove an SLA or a measured 30-day availability rate.
`rns.fyi` describes uptime monitoring but returned HTTP 502 during this review.
Local DNS currently resolves these domains through a proxy's 198.18.0.0/15
addresses, so a successful local TCP connect is not treated as independent
proof of the origin server's availability. Review this list before release;
public services can change. The previous bare-IP candidate `103.195.4.226`
was removed from defaults because a one-off connection was insufficient evidence.

The host regression `test_reticulum_tcp_defaults.cpp` exercises the actual
runtime implementation: default population, isolated edits, invalid inputs,
removal/re-addition, TMS snapshot restoration, retention of an empty TCP list,
legacy custom gateway preservation and LoRa-only policy. It does not simulate
physical SD persistence or live device network connectivity.

The embedded parser accepts at most 2 KB, five nesting levels, 128 structural
tokens, and 128 bytes per JSON string. Its DOM exists only during boot or an
explicit reload and is released before Reticulum applies the new fixed-size
configuration snapshot. Template and last-known-good serialization reuse the
same fixed 2 KB buffer instead of allocating a second output string.

Configuration reload is deferred while a Reticulum call owns the realtime
resource lease. The active interfaces are replaced after the call closes.

## TCP failure handling

Each endpoint backs off after failed connections or disconnects: 10, 20, 40,
80, 160, then at most 300 seconds. A connection must remain up for 60 seconds
before its failure history resets. Changing the endpoint resets its retry state.
Pending asynchronous DNS/TCP work continues to be polled without waiting for
the retry interval. Losing Wi-Fi or socket admission cancels pending work and
defers the next attempt without counting it as an endpoint failure.

The configured TCP entries form an ordered candidate list. Only one TCP uplink
is selected at a time. A failed or deferred attempt yields to the next candidate
whose cooldown has expired; a healthy connection is retained. Once all candidates
are cooling down, the runtime waits rather than opening parallel connections.
AutoInterface LAN peers continue to operate independently. C6-based devices also
retain all three candidates and try them sequentially through their single socket.

Changing a TCP host, port or interface ID, or disabling the interface, discards
both its ordinary and priority receive queues. Reapplying identical settings
preserves queued packets. This prevents data from the old connection from being
reported with a replacement endpoint's interface ID.

Interface IDs are currently 8-bit runtime identifiers. Discovered-candidate
rotation must explicitly retire the old interface's routes and links before
reusing a connection slot; monotonically assigning IDs and eventually wrapping
is not sufficient. Candidate persistence must retain endpoint/access identity,
not treat a runtime slot number as durable identity.

Candidate selection includes the configured entries and one independently
validated, public discovery endpoint. Discovery never modifies the three manual
entries. When configured candidates fail or are cooling down, the same
single-uplink selector can try the discovered endpoint. A healthy uplink is
retained. An empty manual TCP list or a policy that disallows Wi-Fi prevents
automatic admission. One previously stable discovered endpoint can be restored
from SD. A maintained multi-candidate discovery catalog is not yet implemented.

The discovery parser is available in `chat/infra/reticulum/interface_discovery.h`
as groundwork for native `rnstransport.discovery.interface` announcements. Its
wire reference is [Reticulum Discovery.py](https://github.com/markqvist/Reticulum/blob/7f2b3b9b524c9386316379af1313b43a5e4f7a5d/RNS/Discovery.py).
It borrows the payload, allocates no heap memory, and accepts at most 500 bytes
and 32 scalar map entries. The supported subset is a public TCPServerInterface
or BackboneInterface with transport enabled, a host fitting the existing
63-character limit, and a nonzero 16-bit port. Encrypted discovery, IFAC-protected
interfaces, IPv6 endpoints, duplicate keys, and malformed data are rejected.
Parsing alone does **not** authenticate a candidate: the outer announcement
signature and the native 20-round discovery stamp must both be verified before
admission. Its borrowed pointers must not be persisted or retained after the
input expires; the runtime copies only the bounded endpoint metadata.

`DiscoveryStampVerifier` implements the native 20-round, minimum-16-bit stamp
check using the firmware's existing Crypto SHA-256 implementation. It streams
each HKDF output into the final hash, retaining no expanded workblock. Each poll
does one round; another announcement cannot replace a pending check. The object
has a 512-byte size ceiling enforced by the native test, and ESP rejects starting
verification unless the object resides in PSRAM.

`NativeGatewayDiscovery` connects parsing and stamp validation to the adapter's
normal signature-verified announcement path. It permits one verification attempt
per ten seconds, with one in-flight job, including while the L2 screen is awake.
Call, Nomad-request and screen-saver scheduling still take priority. The complete
state, capped at 768 bytes, is embedded in the PSRAM-owned adapter; it retains no
receive-buffer pointers. Only a valid completed stamp replaces the latest
endpoint. Invalid announcements cannot overwrite that endpoint. Network-config
changes reset this temporary state. This is a single observation, not a durable
candidate catalog, and it does not change any configured TCP slot.

The discovered endpoint has a dedicated runtime TCP slot and interface ID 35.
It shares the single active TCP connection budget, including on C6. Replacing
this slot is permitted only while it is neither ready nor connecting. The
adapter first closes links bound to ID 35 and removes its paths, reverse routes
and relays; other interfaces' paths and links remain intact. Deferred discovery
packets are cleared before reuse, and reconfiguration clears both receive queues.
The extra slot and its bounded receive buffers live inside the PSRAM-owned
adapter. They add PSRAM usage; this is not a reduction in total allocated memory.

### Restoring a previously usable gateway

After the discovered TCP connection remains ready continuously for 60 seconds,
`GatewayPersistence` can save its public endpoint. Closing or suspending that
connection resets its stability interval. This checks TCP continuity, not the
reachability of every Reticulum destination through the server.

The cache alternates between `/trailmate/reticulum/gateway.a` and `gateway.b`.
Each file is exactly 114 bytes: magic `RGW1` plus four reserved zero bytes,
little-endian 32-bit sequence, a 64-byte NUL-terminated host, little-endian 16-bit
port, 16-byte announcing network identity, 16-byte transport identity, and a
little-endian CRC-32/ISO-HDLC over the preceding 110 bytes. A write targets the
other slot; the last valid record remains available if the new write is torn.
Load selects the newest valid sequence using wrap-aware comparison. This is a
local cache checksum, not a replacement for network-announcement authentication.

Missing or corrupt records do not prevent normal networking. Busy/unavailable
storage retries at most every five seconds; no record is exposed until both
slots have been examined in the same SD media session. The cache respects USB
ownership and session changes. Unchanged endpoints cause no further file I/O;
successful updates are limited to one per ten minutes. State and the 114-byte
serialization buffer are embedded in the PSRAM adapter with a 384-byte ceiling.

On startup the restored endpoint is offered to the same isolated discovery slot
when there is no newly verified announcement. It does not overwrite manual
configuration or bypass the Wi-Fi policy. New announcements alone never trigger
a cache write. IFAC credentials are not supported by this record format and are
not stored here.

Successful validation emits `[Reticulum][Discovery] verified host=... port=...`.
That log establishes announcement validation only, not server reachability or a
successful connection. These service announcements do not create Contacts or
enter the generic raw-announcement archive, avoiding accidental retention of
credential-bearing discovery metadata there.

The gateway tests include a Python-standard-library reference vector, the real
Crypto implementation, and checks for altered payloads/stamps, cancellation,
incremental progress and PSRAM ownership. To reuse a local Crypto checkout, pass
`-DTRAIL_MATE_CRYPTO_SOURCE_DIR=<directory-containing-SHA256.cpp>` when configuring
`tests/reticulum_gateway`; otherwise CMake fetches a pinned upstream revision.

The native `tests/reticulum_gateway` regression compiles the production connect
and cancel functions with controlled socket, admission and clock adapters. It
covers pending work, cancellation, retry deadlines, clock wrap, stable recovery,
single-uplink selection, candidate failover and cooldown exhaustion;
it does not certify public endpoint reachability.

## Example

```json
{
  "schema": "trail-mate.reticulum",
  "version": 1,
  "interfaces": [
    {
      "id": "integrated-lora",
      "type": "IntegratedLoRaInterface",
      "enabled": true
    },
    {
      "id": "local-wifi",
      "type": "AutoInterface",
      "enabled": true,
      "group_id": "reticulum",
      "discovery_scope": "link",
      "discovery_port": 29716,
      "data_port": 42671
    },
    {
      "id": "primary-tcp",
      "type": "TCPClientInterface",
      "enabled": true,
      "target_host": "vicliu.i234.me",
      "target_port": 4242
    },
    {
      "id": "backup-tcp",
      "type": "TCPClientInterface",
      "enabled": false,
      "target_host": "backup.example.net",
      "target_port": 4242
    }
  ],
  "lxmf": {
    "propagation": {
      "enabled": true,
      "service_enabled": false,
      "delivery_method": "auto",
      "propagation_node": "auto",
      "sync_on_start": true,
      "sync_interval_seconds": 900,
      "max_messages_per_sync": 32
    }
  }
}
```

## Interface Rules

### IFAC interoperability requirement (issue #89)

[Issue #89](https://github.com/vicliu624/trail-mate/issues/89) is part of the
network-access redesign's acceptance scope. IFAC is currently unsupported; the
discovery parser's rejection of IFAC metadata is a temporary supported-subset
boundary, not the intended final behavior.

The implementation must follow the interface-boundary processing in
[upstream Transport.py](https://github.com/markqvist/Reticulum/blob/7f2b3b9b524c9386316379af1313b43a5e4f7a5d/RNS/Transport.py):

- Apply outbound IFAC authentication and masking before bearer framing; remove
  framing, authenticate and unmask inbound IFAC before normal packet parsing.
  Cover RNode/LoRa as well as TCP, including the C6 transport boundary. Merely
  accepting the IFAC packet flag in the existing parser is insufficient.
- Store access configuration per interface, compatible with standard RNS
  `network_name`, `passphrase` and `ifac_size` semantics. Settings must remain
  accessible independently of the active chat protocol and survive TMS reload.
  Do not introduce a global credential shared by unrelated interfaces.
- Keep discovered endpoint metadata and its access profile associated through
  persistence and failover. Host and port alone are not a sufficient candidate
  identity when access profiles differ. A failed private connection must not
  retry that same interface without its configured IFAC protection.
- Handle publicly advertised `IFAC_NETNAME`/`IFAC_NETKEY` explicitly once the
  codec is available. Announcements must not overwrite user-managed credentials.
  Private credentials remain configurable without publishing them. Discovery
  announcement encryption and interface IFAC are separate mechanisms.
- Use strict PSRAM ownership for retained credential/codec state and bounded
  scratch buffers. Account for the IFAC bytes in frame capacities and MTU checks.
  Never log passphrases, derived keys or complete credential-bearing payloads.

Acceptance requires bidirectional vectors generated by Python Reticulum for
name-only, passphrase-only and combined access configurations, supported IFAC
sizes, wrong credentials, missing/unexpected IFAC flags, tampering and truncated
frames. Add configuration round-trip, protected/unprotected interface isolation,
candidate failover and RNode framing tests. Preserve existing public-network
behavior. Device interoperability and memory measurements remain required before
claiming IFAC support; neither a successful TCP socket nor a valid discovery
stamp establishes IFAC interoperability.

### Currently supported interfaces

- `IntegratedLoRaInterface` enables the board's integrated LoRa bearer. Only
  one entry is allowed.
- `AutoInterface` implements the official Reticulum IPv6 link-scope discovery
  and per-peer UDP interface model. Only one entry is allowed.
- `TCPClientInterface` connects to a Reticulum TCP server or gateway. Up to
  three entries can be configured on boards with native Wi-Fi.
- T-Display-P4 uses the C6 companion's single TCP transport and selects one
  configured TCP candidate at a time, failing over after a connection failure.
  It does not expose an IPv6
  AutoInterface through the companion transport.
- Unknown destinations and announces may fan out over ready interfaces.
  Learned paths, links, proofs, resources, calls, and Nomad requests remain
  bound to the exact ingress interface or learned path interface.

## LXMF Delivery

`delivery_method` accepts:

- `direct`: use opportunistic or direct-link delivery only.
- `propagated`: submit messages to a selected propagation node.
- `auto`: prefer an existing direct link, then opportunistic delivery with a
  usable ratchet, then a discovered propagation node, and finally establish a
  direct link when no propagation node is available.

`propagation_node` can be `auto` or a 32-character propagation destination hash.
Automatic selection prefers an active, fresh node with the lowest known hop
count. Trail Mate generates the official LXMF propagation stamp incrementally,
uploads the encrypted message over an identified propagation link, and marks
the message sent to the propagation node only after the link packet or resource
proof is validated. This state does not claim final recipient delivery.

`service_enabled` is intentionally `false` by default. Enabling it makes this
battery device announce and accept traffic as an LXMF propagation service;
normal message-for-propagation client support does not require it.

During synchronization Trail Mate requests the remote transient-ID list,
downloads only missing messages addressed to its local LXMF delivery
destination, and acknowledges handled IDs. Seen transient IDs are retained in
the bounded propagation runtime to suppress duplicate delivery.
