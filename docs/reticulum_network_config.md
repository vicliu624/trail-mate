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

In **Settings → Mesh**, select **TCP Entry** 1, 2 or 3,
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
These UI fixes have passed targeted syntax checks and the TCP restore host
regression. The updated L2 firmware was built and flashed on 2026-09-24;
physical verification of the restore action and public TCP connection remains pending.

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

- `IntegratedLoRaInterface` enables the board's integrated LoRa bearer. Only
  one entry is allowed.
- `AutoInterface` implements the official Reticulum IPv6 link-scope discovery
  and per-peer UDP interface model. Only one entry is allowed.
- `TCPClientInterface` connects to a Reticulum TCP server or gateway. Up to
  three entries can be configured on boards with native Wi-Fi.
- T-Display-P4 uses the C6 companion's single TCP transport and therefore uses
  only the first enabled `TCPClientInterface`. It does not expose an IPv6
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
