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

Presence sends authenticated status about every 30 seconds, with a per-node
offset. Member presence touches the sender without replacing the authoritative
leader roster. Protocol, region, actual frequency, modulation and channel keys
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

## Verification

Native fixtures cover compatible crypto and tamper rejection, private snapshot
save/reload and truncation rejection, authenticated presence, relay/delivery
deduplication, newest-position projection, SX1262 standby/FIFO sequencing and RX
restoration, Contacts Team visibility, and real worker queueing under carrier
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
