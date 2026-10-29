# Native LXMF peer-text sending

The native mesh worker implements the existing destination-keyed text port and
advertises peer-text and protocol-acknowledgement capabilities. The application
chat service and message ledger retain message history and state. UI submission
packs bytes for size validation; keys, signatures, path/link work and radio I/O
remain on the worker. No numeric-node fallback or TML1 representation is used.

At most eight unresolved intents await keys or paths. Ordinary path requests
learn verified peer keys/routes; retries respect the shared cooldown, and
discovery expires after 45 seconds. Intents rotate so one unavailable peer does
not block another. Ignored peers and directory errors fail without initiating
transmission. Authenticated LinkIdentify keys also enter the existing directory
service, preserving local trust, ignore, favorite and alias policy. An active
incoming delivery link can therefore be reused for a reply without inventing an
announcement path.

Queued envelopes use the canonical session's deferred payloads, and the shared
`DeliveryAttemptLedger` owns sent-packet receipts. Outstanding transport work is
bounded. A loop iteration sends at most one queued data packet, and initiator
keepalives use the shared lifecycle predicate. Fractional timestamps distinguish
successive identical texts within a second. Envelopes exceeding the negotiated
single-packet MDU use encrypted resources within the shared allocation limits.
Linux compresses resource data with bzip2 only when this reduces its size.
Resource and ordinary message scheduling alternate so a large transfer does
not prevent another conversation from sending a short message.

| State | Required fact |
| --- | --- |
| Queued | Accepted by the application/worker queue; discovery or handshake may remain pending |
| Sent | The complete RNode packet set was accepted by the physical TX port |
| Delivered | A packet proof verifies against its active link and expected signing key, or a resource proof matches the active link, resource hash and original-data completion digest |
| Failed | Physical failure, rejected size, discovery/link/proof timeout, reconfiguration or shutdown cancelled the attempt |

Unknown, wrong-link, tampered or duplicate proofs cannot fabricate Delivered.
Physical failure does not emit Sent. Receipt expiry reports AckTimeout, while
payload rejection and cancellation are distinct from radio failure. Outcomes
retain their original protocol when the UI switches protocols.

The integration regression runs real LinuxAppServices, chat service, SQLite and
radio coordination against a fake packet radio and another native link owner.
It discovers an unknown peer, establishes a link, sends 280 bytes over two
physical RNode packets, checks withheld/tampered/wrong-link/duplicate proofs,
and receives a reply through durable application acceptance. It also checks
physical failure, pending-path cancellation and radio thread ownership. Native
owner tests cover receipt expiry and both peer roles.

This supports plain peer text in link packets and unsplit resources without
metadata. Custom/application fields, split resources, opportunistic/group
messaging, propagation and LXST remain unfinished. Software
peers and QEMU do not establish physical on-air interoperability.

Text readiness does not enable legacy VMP recording on Reticulum. The native
voice service explicitly retains Meshtastic/MeshCore recording eligibility;
Reticulum telephony remains a separate unfinished LXST port.

Verification: the native suite passed 93/93 tests, including the new application
send/receipt/reply flow and VMP protocol gate; the compact suite passed 14/14.
The link owner and canonical receipt owner run with ASan/UBSan in the native
regression. Assertion-enabled armhf sender, link, peer and mesh-worker tests
passed under QEMU. The relocated portable runtime-state contract passed, and
20 relevant P4 translation units passed installed cross-compiler syntax checks.
The rebuilt standard Debian package passed extracted-application bootstrap,
1232 x 568 rendering, and Logo/README checks. No full P4 firmware link/upload or
physical RF peer test is implied.
