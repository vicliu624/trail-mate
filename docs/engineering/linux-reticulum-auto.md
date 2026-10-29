# Native Reticulum AutoInterface

The native UDP bearer is implemented and connected to
`LinuxReticulumNetwork` and deployed in the CM0 package recorded below.

`LinuxReticulumIpFrame` now supplies the bearer-neutral frame contract. TCP
retains its existing nested `Frame` alias. The network owner keeps TCP and Auto
owners separate and routes a frame through its original logical interface ID
and peer/connection generation. Its bounded route slots accommodate three TCP
connections and sixteen Auto peers. Kernel interface indices never become the
application's logical interface IDs.

## Discovery and datagrams

The implementation follows the link-scope IPv6 discovery and UDP peer behavior
of the [official Reticulum AutoInterface](https://github.com/markqvist/Reticulum/blob/master/RNS/Interfaces/AutoInterface.py).
Group IDs and discovery/data ports come from the existing network configuration.
Discovery authentication uses the group and canonical source address; it is
separate from IFAC packet access control. UDP carries raw Reticulum packets,
without TCP HDLC framing.

The owner enumerates up, multicast-capable, non-loopback link-local IPv6
addresses using `getifaddrs` and kernel indices. It adopts at most eight local
interfaces without assuming NIC names. Socket scope and received packet-info
must match. A source must complete discovery before its data is accepted.
Sockets are nonblocking and close-on-exec; multicast membership is confined to
the selected link. Discovery, reverse discovery and data have distinct ports;
the discovery port must leave room for the reverse port at discovery + 1.

The shared `InterfaceAccessCodec` handles both protected and public traffic.
Invalid IFAC never falls back to public decoding. Native packets remain bounded
by the existing 500-byte Reticulum MTU, plus the configured IFAC tag on wire.
Link signalling negotiates that supported MTU with larger-capacity RNS bearers.

## Lifetime and scheduling

Local interfaces are rescanned every four seconds. Address removal/change
closes old sockets, retires their peers and prevents queued old-generation data
from being delivered. Valid discovery/data refreshes peers; silence expires a
peer after 22 seconds. Multicast and reverse announcements use the reference
timing defaults. Unchanged Auto configuration retains peers when unrelated TCP
configuration changes. A changed Auto owner is prepared before the TCP update
commits, so invalid updates preserve live transports.

Receive storage is a sixteen-frame FIFO. A 48-entry, 750 ms wire-hash window
suppresses duplicate delivery across links. Invalid/full-queue packets do not
poison that window. Each tick is bounded by 64 datagrams and 8192 received bytes,
with rotating socket order and an eight-packet per-socket quota. New peers are
rejected with a counter when the sixteen-peer table is full; established peers
are not evicted to admit traffic. Socket enumeration, polling, IFAC and frame
work stay on the protocol worker, never the UI thread.

The existing network owner handles signed announcements, path requests and link
bindings for either bearer. Peer expiry or scope change retires the associated
paths and links. A link cannot silently move its ciphertext to another peer or
connection generation.

## Configuration and platform capabilities

Generic configuration/TMS admission accepts Auto and TCP IFAC records. The ESP
adapter still supports IFAC only on its TCP bearer and rejects unsupported Auto
access during validation, before applying any settings owner. A native-to-ESP
document test checks that rejection occurs in the validation pass.

HostLink advertises `CapAutoInterfaceIfac` (bit 14) for the native bearer. The web
editor gates Auto access controls on this feature bit, without model-name
checks. Existing access records can always be removed to repair a document.
Firmware without the bit keeps its existing TCP controls. The complete existing
document schema and native transactional import/restart boundary remain in use.

## Evidence and limits

Nine wire vectors are generated with pinned RNS 1.5.4. The independent reference
test runs the official RNS composition in a separate network namespace and the
native owner across a veth pair. Public, 128-bit IFAC and 512-bit IFAC cases passed
500-byte bidirectional data, malformed discovery/IFAC rejection, duplicate
suppression, interface rename/readdress recovery and peer expiry. The production
network owner also emitted a reference-validated signed announcement and path
request and established a real RNS link with its negotiated MTU checked at 500.

The focused native batch passed 23/23, covering TCP, endpoint routing, calls,
pending path/key discovery, TMS and the production HostLink web client. The ESP
HostLink/TMS suite passed 127/127; the relevant web configuration/label tests and
the device editor build passed. The full native run passed its other 187
fixtures; its one old fixed-capability assertion was updated for bit 14 and
passed a focused rebuild/retest. Cardputer Zero rebuilt and passed 19/19.

Five ARM fixtures passed on CM0: Auto reference vectors/configuration, TCP,
network routing, calls and TMS lifecycle. The installed formal application
additionally opened the configured discovery/reverse/data UDP ports on its
available IPv6 links. A real TCP configuration commit caused exec; the test
verified new configuration, receipt replay and replacement of the old UDP socket
inodes before orderly exit. It used temporary data roots and dummy SDL devices,
with no screenshots, existing-interface changes or display-driver changes.

The standard `trailmate-cm0_1.0.0_armhf.deb` passed lintian, installation,
`dpkg -V` and the original-logo hash check. Its SHA-256 is
`e9596e0f6adcc057c7d043fee0dcf9ac571d5bdb91e4cf3300a28647238252fa`;
the installed executable is
`46b7decbf5f14f26fbba900c9a9102d4c315c787018ab936fb7c5d08c9f051f2`.
The profile still uses `screen_power=none`. External CM0 peer interoperability
and LoRa on-air operation were not established by these tests.

The isolated reference runner requires root network-namespace support and the
pinned Python environment. Run it inside `unshare -n`; it creates only temporary
veth links and a child namespace, never modifies an existing host interface, and
reaps both processes on exit. It is intentionally separate from unprivileged
routine CTest fixtures.
