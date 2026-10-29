# Linux MeshCore node information

The Linux mesh adapter uses the production NodeInfo control codec and
`MeshCoreRuntime::handleIncomingNodeInfoControl` for query replies and node
projection. This is separate from native signed MeshCore advertisements.
Broadcast reannouncement retains its signed advertisement so public-key
discovery continues to work. Directed query and information sends use the
existing encrypted private carrier; broadcast queries use the configured group
channel. Replies reuse the shared runtime's peer/channel selection.

NodeInfo controls terminate in the protocol engine and never enter the ordinary
application-data inbox. Existing signature de-duplication suppresses repeated
requests. Private information is bound to its authenticated sender: a claimed
node ID for another peer cannot update that peer. Information controls do not
grant or revoke public-key trust. MQTT input retains network provenance and
does not authorize RF or produce measured RSSI/SNR facts.

## Fixed control padding

The legacy GD/direct application envelopes have no arbitrary payload-length
field. Their ordinary trailing-zero rule cannot safely represent all binary
payloads. NodeInfo is an existing fixed-size exception: its query flag and
fixed-width names legitimately end in zero bytes. Shared helpers retain the
defined control size and reject nonzero bytes after that size. The direct path
retains its existing fixed-size key-verification and PATH/ACK exceptions.
Other application payloads are unchanged; Linux still rejects ambiguous
zero-ended arbitrary sends. No new wire format or protocol version is added.

Linux group and direct codecs use this rule. The ESP group-data ingress uses
the same helper; its direct ingress already calls the shared peer-padding
helper. The correction adds no ESP frame buffers or protocol objects to its
task stack.

## Verification

The native NodeInfo fixture passed V1/V2 group encoding, exact zero-tail
recovery, padding rejection, encrypted directed query/reply, broadcast query,
duplicate suppression, private identity binding, key-state preservation and
TX-disable admission. The wire codecs and padding helper are instrumented with
ASan/UBSan. Existing group, direct, repeater-adapter and MQTT application
regressions passed in the same targeted gate (5/5).

The actual MQTT application fixture additionally requests full node information
after signed-key discovery and checks distinct short/long names, network
provenance, absence of RF measurements, preservation of discovered keys and
reopening the persisted name. Target and package evidence is recorded in the
[CM0 integration ledger](t-display-cm0-hardware-integration.md).

Those application assertions passed over both the independent wire relay and
Mosquitto 2.0.18 on the host. Four ARM groups passed on the actual CM0: NodeInfo,
group, direct and the application MQTT pair. Both Cardputer Zero runtime targets
rebuilt, with 19/19 compact tests passing. A previously validated armhf deb
containing these changes has SHA-256
`8ea6b4213c1359338fac1c53f75b77ffad5cfcf7295f143d0c6b4c05018baf53`.
Lintian, installation, `dpkg -V`, original-logo validation and isolated installed
startup/timed exit passed. ESP source uses the shared helper, but no additional
ESP firmware build or physical peer-device RF test was performed in this pass.
See the [CM0 acceptance audit](cm0-adaptation-acceptance.md) for the current artifact.
