# Native LXMF link-text reception

The native mesh worker now connects authenticated link text to the existing
`ChatService`, SQLite message ledger and `IIncomingDeliveryCommitPort`. Radio,
session keys, packet parsing and proof transmission remain on the mesh worker.
UI callers poll copied messages and enqueue identity-only commit tokens. They
do not perform crypto or radio I/O.

The link owner accepts an encrypted context-NONE packet only on an active,
validated link and within its negotiated MDU. The shared envelope helper handles
both full LXMF envelopes and destination-omitted RNS tails. The local destination
must match. The source signing key comes from the persisted peer directory or
the link's authenticated identity declaration. Directory keys are checked by
recomputing their delivery-destination binding. Ignored sources and directory
errors fail closed. A known link identity cannot be replaced by a different
identity declaration.

After signature verification, the shared production materialiser builds the
incoming text with its full peer identity, full LXMF hash, encrypted/verified
facts and RX metadata. The native worker queues the message for the same chat
service used by application composition. The service runs reception even when
outgoing-message readiness is false; the existing UI disables reply according
to send capabilities while retaining conversation/history display.

The link owner retains at most 32 pending acceptance records. These hold link,
peer, message-hash and packet-hash facts, not a second copy of message content.
Duplicate pending messages coalesce; a full pool or full application queue
rejects the new packet without displacing accepted work. Records expire after
120 seconds. Protocol/radio reconfiguration, power-off and shutdown reset
transient protocol state. The worker's separate bounded commit queue coalesces
full hashes and cannot run a stale receive epoch against a new protocol context.

Only a positive durable application commit permits a Reticulum packet proof.
The proof signs the canonical incoming packet hash using the responder identity
key or the initiator's link signing key, as appropriate. A negative commit drops
the acceptance record without a proof. A physical proof-send failure retains
the durable token for retry at one-second intervals. Store-backed duplicate
acceptance lets retransmissions recover a lost proof without adding a message
or unread count. This remains an incoming transport acknowledgement, not an
outgoing Delivered event fabricated by the receiver.

Current scope is plain text in a single link packet or an encrypted unsplit
resource, with a known verifiable source. Resource data can be bzip2-compressed;
expansion must produce exactly the declared size within the shared limit.
Resource completion proofs also wait for durable application acceptance. A
failed application commit retains encrypted parts for a bounded retry rather
than acknowledging data that was not saved. Nonempty custom/telemetry fields,
application data and resource metadata are not acknowledged through this text
path. Unknown-source materialisation, opportunistic/group reception, split
resources, propagation and LXST remain
unfinished. Plain peer-text sending is now connected separately through the
existing destination port; see [Native outgoing peer text](linux-lxmf-outgoing.md).

The native link regression covers bad signatures, directory-key binding,
identity-declaration fallback, ignored peers, full and omitted destinations,
duplicate pending input, wrong/negative commits, proof-send retry, proof
signature/hash verification and bounded acceptance. The real worker integration
feeds RNode-framed requests, RTT, identify and signed text through a fake packet
radio, then runs the actual chat service and SQLite store. An injected database
failure produces no proof; recovery produces a proof; retransmission preserves
the row/unread counts. The fake driver detects any physical I/O on the UI thread.
Software evidence does not establish physical peer interoperability.

This stage passed 92/92 native tests and 14/14 compact tests. The native link
owner and shared envelope/materialisation sources run with ASan/UBSan/leak
checks. The portable runtime-state contract passed after shared-core relocation,
and 19 related P4 translation units passed installed cross-compiler syntax
checks. Assertion-enabled armhf link, peer-directory and application-worker
regressions passed under QEMU, including the durable receive/proof path. The
rebuilt standard Debian package passed extracted-application bootstrap,
1232 x 568 rendering and Logo/README checks. No complete P4 firmware link/upload
or physical peer/on-air result is implied.
