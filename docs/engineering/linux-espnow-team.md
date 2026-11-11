# Linux ESP-NOW Team integration

CM0 and M5Stack Cardputer Zero use the shared Team coordinator, protocol and
service to pair with an unchanged ESP32 TrailMate. ESP-NOW handles enrollment;
subsequent Team status, chat and map data use the selected mesh bearer.

## Qualified pairing hardware

| Device | OS / architecture | Kernel | Wi-Fi |
| --- | --- | --- | --- |
| T-Display-CM0 | Raspbian / ARMv7 hard float | 6.18.50-trailmate-irq-v7 | CYW43439 |
| M5Stack Cardputer Zero | Debian / AArch64 | 6.18.39+rpt-rpi-v8 | CYW43439 |

The qualified devices retain brcmfmac firmware 7.95.75, FWID 01-bb9b5b66.
The helper uses the existing Broadcom vendor DCMD `actframe` transmit operation
and cfg80211 management-frame receive registration. These combinations do not
require monitor mode, firmware replacement or kernel patches. This does not
qualify arbitrary Linux Wi-Fi adapters.

The vendor action body contains category 127, Espressif OUI `18:fe:34`, four
random bytes and a vendor element with ESP-NOW type 4/version 1. Application
payloads are bounded to 250 bytes. The firmware reconstructs parts of the
received MAC header; decoding uses its source address and action payload,
without treating that reconstructed header as an exact air capture.

Pairing temporarily detaches wlan0 from NetworkManager and selects channel 1
through the firmware setter with readback. Reception registration follows that
transition. Firmware MPC/PM sleep is disabled for the connectionless window;
the previous policy is saved durably before modification and restored afterward.
Concurrent ordinary Wi-Fi and ESP-NOW operation has not been qualified.

## Installation and privilege boundary

From the repository root on a qualified device:

```sh
sudo sh scripts/install_linux_espnow_helper.sh
```

The installer requires systemd, NetworkManager, Python 3, iw, ip, rfkill,
`/usr/include/linux/nl80211.h` and the local `pi` account. Build and install the
Linux application separately. The helper and units currently have a standalone
installer; they are not automatically included in the Debian package lifecycle.

The application runs as the ordinary user. `LinuxEspNowPairing` connects the
shared `TeamPairingCoordinator` to a bounded Unix-domain protocol. Root radio
operations stay in the socket-activated Python helper. Its root:pi socket is
mode 0660 and checks peer credentials. Clients may send bounded ESP-NOW payloads
to peer MACs; the protocol does not expose arbitrary firmware commands or channel
changes. Production pairing is available only when the installed control socket
is accessible. Demo mode retains its separate loopback transport.

## Recovery independent of the application

Before changing wlan0, the helper verifies the independent recovery timer, a
saved active connection UUID and an IPv4 default route. It fsyncs a root-only
lease containing the boot ID, deadline and preceding radio policy.

| Limit | Duration |
| --- | --- |
| Active enrollment exchange | 125 seconds |
| Recovery lease | 150 seconds |
| Independent timer interval | 15 seconds |
| Helper service runtime limit | 165 seconds |

Normal completion, cancellation and client EOF expire the lease and request
recovery in a separate unit. Killing the application or helper cannot stop the
independent timer. Recovery restores NetworkManager ownership, the saved power
policy, connection UUID and default route. A failed activation retains the
record for retry; only verified recovery clears it. A changed boot ID triggers
recovery of an interrupted lease after reboot.

Physical SIGKILL acceptance restored ordinary Wi-Fi, SSH, route and the original
`mpc=1, pm=2` policy at 154 seconds on CM0 and 155 seconds on Zero, measured
from arming the lease. Reboot detection and reconnect retries also have automated
coverage. These measurements do not promise a fixed reconnection time for every
network.

## Durable Team state and compatible crypto

Team ID, PSK, key round, membership, name and transient pairing flags persist
atomically in a private mode-0600 `team.snapshot`. Startup restores controller
keys from that snapshot. There is no time-based expiration of membership or
credentials. A missed member status does not clear them; an explicit leave does.

The normal device paths are:

- CM0: `~/.local/state/trailmate/team.snapshot`
- Zero: `~/.trailmate_cardputer_zero/team.snapshot`

A local snapshot member with node ID zero represents “You”, not a zero radio
identity. The ESP-NOW key exchange does not carry the full roster; authenticated
mesh status messages subsequently supply the leader's roster.

Native Linux crypto matches the ESP implementation: SHA-256 over PSK followed
by the derivation label, then ChaCha20-Poly1305 with a 12-byte nonce and an
appended 16-byte tag. It fails closed without OpenSSL. The former Linux demo
cipher is not used for native Team traffic.

Chat display history is currently process-local on Linux. Restarting an
application clears its displayed Team conversation history while retaining
membership and keys.

## Common Linux mesh dispatch

Both devices use the same Team service, snapshot store, crypto implementation,
mesh worker, presence scheduler and Meshtastic relay planner. Their physical
drivers differ: CM0 uses LR2021; Zero uses the SX1262 CAP. SX1262 transmission
enters checked standby before changing packet parameters or writing the shared
RX/TX FIFO, then restores reception and reports errors.

The shared Team background dispatcher sends authenticated status every 30
seconds after a successful enqueue, with a five-second startup/retry delay.
This schedule continues after the finite enrollment broadcasts have drained and
does not depend on keeping the Team page open. Leaders send their complete
roster; members send presence without a roster, retaining the leader's list.
Linux uses the same dispatcher rather than a second platform scheduler. A
single 32-bit deadline is added to the dispatcher; periodic sends do not allocate
queue entries. Existing finite enrollment retries retain their compatibility
messages, while periodic presence is authenticated only.

Arduino, ESP-IDF and Linux UI lifecycle adapters all call the same system-tick
publisher before dispatching UI events. It attempts at most one event per
second, including after queue backpressure, and handles millisecond rollover.
The publisher has eight bytes of clock state and uses the existing event bus;
it adds no task, queue capacity or protocol buffer. A SystemTick consumer alone
does not schedule work: the ESP lifecycles must also produce that event.
The initial Team state load is completed only after a successful read, so an
early tick cannot permanently skip restoration while ESP storage is starting.
Failed reads preserve the existing in-memory Team identity and credentials.

Protocol, region, actual frequency, modulation and channel keys
must agree across participants. The live CN / LongFast exercises used
478.875 MHz, BW 250 kHz, SF11, CR 4/5, sync 0x2B and a 16-symbol preamble.

For physical Meshtastic application data, the worker observes completed RX/TX
and damaged-frame counters plus instant RSSI. It defers local data through the
active packet and delayed relay exchange. The guard derives from configured
LoRa airtime and relay delay. Every new attempt, including a command that wakes
an idle worker, first enters randomized contention.

Callers enqueue without sleeping. Receive processing, due relays and the UI
continue while application data waits. A continuously busy channel expires an
operation after 30 seconds, with an action error and a
`Meshtastic queued TX expired` packet log entry. It does not clear Team
credentials. MQTT and other mesh protocols retain their own transport policies.

The bounded relay planner preserves origin, packet ID and encrypted payload,
decrements hops and marks the relay node. It does not forward MQTT ingress to
RF. Retries and queued frames have bounded lifetimes. A separate delivery cache
prevents original RF, relay and MQTT copies from generating duplicate application
events; reliable direct duplicates still receive routing ACKs.

The position-ring reader projects the newest timestamp per member. An older
relayed sample cannot replace a newer displayed location.

Team relative-time presentation uses wall-clock seconds when available and
does not subtract a process uptime from a Unix timestamp. A persisted timestamp
from a different clock domain is shown as unknown. The local zero-ID alias is
the running observer, not a stale RF peer; returned local radio identities map
back to that same alias instead of adding another member. Snapshot reload also
restores the encoded last-update timestamp.

## Verification

Native fixtures cover compatible crypto and tamper rejection, private snapshot
save/reload and truncation rejection, authenticated presence, relay/delivery
deduplication, newest-position projection, SX1262 standby/FIFO sequencing and RX
restoration, rejection of simultaneous RX_DONE/CRC/header error IRQs, Contacts
Team visibility and conversation routing, and real worker queueing under carrier
activity. Queue tests also cover idle-worker wake-up, eventual transmission,
timer rollover and CPU-spin avoidance.

Run the recovery tests on Linux:

```sh
python3 -m unittest discover -s tests/linux -p test_espnow_recovery.py
```

Physical pairing acceptance runs `trailmate_linux_team_espnow_hardware_test`
explicitly, outside CTest: it temporarily changes wlan0 and requires the installed
recovery helper. Use isolated settings roots and validation identities for that
executable. Normal installed-app acceptance must use the user's actual identity
and preserve its current snapshot.

Enqueue success or a local “Sent” bubble alone does not prove peer reception.
Correlate actual RF packet IDs at the receiver, authenticate the current Team
envelope, and inspect the peer conversation. Enrollment success and matching keys
alone do not qualify subsequent chat or map traffic.

Map acceptance needs a usable GNSS fix or an explicitly labeled test waypoint.
Manual test coordinates must not be presented as a live fix. A “Team location not
ready” result when no fix exists is distinct from a radio delivery failure.
Independent application-resume timers must protect any exclusive-radio diagnostic
that temporarily stops the normal app.

## Shared protocol ownership

Linux is a useful test host for the protocol code that also runs on ESP32. The
same protocol must not acquire a second wire implementation merely because its
radio or operating system changes. Team enrollment and mesh transport remain
separate; successful ESP-NOW enrollment does not qualify a mesh bearer.

| Protocol | Canonical shared implementation | Platform responsibility |
| --- | --- | --- |
| Meshtastic | `core_chat` packet wire format, nonce, channel framing, protobuf helpers, PKI primitives and runtime effects | Radio I/O, scheduler, persistent identity, event/store integration; the AES primitive selects the existing ESP implementation or Linux OpenSSL backend |
| MeshCore | `core_chat` crypto, payload helpers and identity primitives; `core_mesh::MeshCoreProtocolStrategy` builds both group and private AppData | Resolve channel/peer keys and paths, maintain bounded pending sends, persist identities, and project received events |
| Reticulum / LXMF | `core_chat` packet/token format, signature and handshake calculations, packet router, path/session/resource owners and delivery planning | Interface sockets/radio, clock/random/crypto providers, persistence, allocation budget and application acceptance |

`LinuxRawLoraMeshAdapter` calls Meshtastic `buildWirePacket`, `parseWirePacket`
and `decryptPayload`; it does not maintain its own AES-CTR, nonce or packet
codec. The Linux OpenSSL primitive operates in place. ESP preprocessing excludes
that backend, including its OpenSSL context allocation.

Linux MeshCore group/private AppData calls the same strategy used by the ESP
adapter. Explicit public-key hashes take precedence in both V1 and V2, including
a valid zero first byte. Oversized AppData fails instead of silently transmitting
a truncated application envelope. Linux admission checks still reject ambiguous
zero-padded application tails and disabled channel slots. Existing text and PATH
ACK composition, platform relay scheduling and adapter orchestration have not
all been consolidated; sharing AppData does not claim that the whole adapter is
platform independent.

Reticulum's ESP compatibility headers already forward to canonical `core_chat`
owners. Keep that direction of dependency. Linux's heap buffers, SQLite
transactions and socket lifecycle must not become ESP task-stack buffers or a
second copy of the shared session/resource runtime. Remaining platform envelope
orchestration can be reduced only while preserving its caller-owned storage and
durable acceptance boundary.

### Memory and compatibility checks

The Meshtastic backend change produces byte-identical ESP preprocessed code.
An ESP32-S3 `-Os -fstack-usage` compile of the MeshCore strategy before/after the
AppData convergence reports 3,266 bytes of code and zero static data/BSS for both
translation units. The send implementation's reported stack frame changes from
688 to 704 bytes; its wrapper remains 48 bytes and RX remains 416 bytes. These
are isolated compiler checks, not whole-firmware RAM or runtime high-water
measurements. No strategy member, queue, allocation or buffer capacity is added.

Native contracts independently decrypt actual Linux Meshtastic AES-128/256
packets with OpenSSL and compare their complete bytes with the shared codec.
MeshCore contracts compare group/private V1/V2 AppData with the shared strategy,
retain independent encrypted text vectors, and test explicit zero hashes and
oversize rejection. Cardputer Zero registers those existing MeshCore fixtures
in its native suite, so they run against the same common library as the device.
The repository ESP stack hygiene check remains required. Software contracts do
not establish new MeshCore or Reticulum on-air qualification.
