# Meshtastic Phone API / BLE Protocol Reference

## Purpose

This document summarizes the Meshtastic phone-facing protocol as implemented by the official upstream code currently vendored in this repo:

- Firmware: `.tmp/firmware`
- Apple app: `.tmp/Meshtastic-Apple`
- Android app: `.tmp/meshtastic-android`

The goal is to answer protocol and encoding questions from source-grounded rules instead of relying on local assumptions.

This document focuses on:

- `ToRadio` / `FromRadio`
- BLE `fromNum` / `fromRadio` interaction
- `QueueStatus`
- `MeshPacket.id`
- `decoded.request_id`
- `decoded.reply_id`
- `want_ack`
- broadcast vs direct-message behavior
- how official apps interpret ACK/NAK state

## Source Anchors

Primary firmware sources:

- `.tmp/firmware/src/mesh/PhoneAPI.h`
- `.tmp/firmware/src/mesh/PhoneAPI.cpp`
- `.tmp/firmware/src/mesh/api/PacketAPI.cpp`
- `.tmp/firmware/src/mesh/StreamAPI.cpp`
- `.tmp/firmware/src/mesh/Router.cpp`
- `.tmp/firmware/src/mesh/ReliableRouter.cpp`
- `.tmp/firmware/src/mesh/MeshService.cpp`
- `.tmp/firmware/src/mesh/MeshModule.cpp`
- `.tmp/firmware/src/modules/RoutingModule.cpp`
- `.tmp/firmware/src/mesh/generated/meshtastic/mesh.pb.h`

Official app sources:

- `.tmp/Meshtastic-Apple/Meshtastic/Accessory/Accessory Manager/AccessoryManager.swift`
- `.tmp/Meshtastic-Apple/Meshtastic/Accessory/Accessory Manager/AccessoryManager+FromRadio.swift`
- `.tmp/Meshtastic-Apple/Meshtastic/Accessory/Accessory Manager/AccessoryManager+ToRadio.swift`
- `.tmp/Meshtastic-Apple/Meshtastic/Accessory/Transports/Bluetooth Low Energy/BLEConnection.swift`
- `.tmp/Meshtastic-Apple/Meshtastic/Helpers/MeshPackets.swift`
- `.tmp/meshtastic-android/app/src/main/java/com/geeksville/mesh/service/PacketHandler.kt`
- `.tmp/meshtastic-android/app/src/main/java/com/geeksville/mesh/service/FromRadioPacketHandler.kt`
- `.tmp/meshtastic-android/app/src/main/java/com/geeksville/mesh/service/MeshDataHandler.kt`
- `.tmp/meshtastic-android/core/model/src/commonMain/kotlin/org/meshtastic/core/model/DataPacket.kt`

## Big Picture

Meshtastic exposes a "phone API" over multiple transports:

- BLE
- serial stream
- packet/IPC API
- HTTP variants

Across those transports, the logical payloads are the same:

- phone -> device: `ToRadio`
- device -> phone: `FromRadio`

The transport framing differs, but the application-level meaning does not.

Important consequence:

- If we want to know "what the phone is supposed to believe", we must follow `PhoneAPI.cpp` plus the official app code.
- If we want to know "what ACK means", we must follow `Router.cpp`, `ReliableRouter.cpp`, `MeshModule.cpp`, and app-side routing handling.

## Transport Layer Rules

### Serial / stream transport

`StreamAPI.cpp` shows the serial framing:

- start bytes: `0x94 0xC3`
- then 16-bit big-endian payload length
- then protobuf bytes

Payload directions:

- toward device: `ToRadio`
- toward client: `FromRadio`

This is transport framing only. After decoding, the same `PhoneAPI` logic applies.

### Packet API transport

`PacketAPI.cpp` wraps the same behavior in queued protobuf packets.

Important rules:

- `ToRadio.packet` is passed into `service->handleToRadio(*mp)`
- `ToRadio.want_config_id` starts the config state machine
- `ToRadio.heartbeat` is handled
- outgoing `FromRadio` packets are produced by `getFromRadio()`

### BLE transport

Official BLE behavior is split between:

- firmware BLE implementation
- `PhoneAPI`
- app BLE client logic

Upstream firmware exposes:

- `TORADIO`
- `FROMRADIO`
- `FROMNUM`
- `LOGRADIO`

Official Apple BLE behavior in `BLEConnection.swift`:

- phone writes protobuf bytes to `TORADIO`
- phone receives `FROMNUM` notification
- after `FROMNUM`, phone drains pending packets by repeatedly reading `FROMRADIO`
- drain ends when `FROMRADIO` read returns empty payload

So `FROMNUM` is not the data itself. It is a wakeup/edge signal telling the client that one or more `FromRadio` packets are ready.

## PhoneAPI State Machine

`PhoneAPI.cpp` contains the canonical state machine for what the phone receives.

States are:

1. `STATE_SEND_NOTHING`
2. `STATE_SEND_UIDATA`
3. `STATE_SEND_MY_INFO`
4. `STATE_SEND_OWN_NODEINFO`
5. `STATE_SEND_METADATA`
6. `STATE_SEND_CHANNELS`
7. `STATE_SEND_CONFIG`
8. `STATE_SEND_MODULECONFIG`
9. `STATE_SEND_OTHER_NODEINFOS`
10. `STATE_SEND_FILEMANIFEST`
11. `STATE_SEND_COMPLETE_ID`
12. `STATE_SEND_PACKETS`

Important rule explicitly documented in code:

- client apps assume this config-send order
- upstream comments say: "DO NOT CHANGE IT"

### Config start

When the device receives `ToRadio.want_config_id`:

- `PhoneAPI::handleStartConfig()` is called
- the connection is considered active
- the device enters the config-send sequence
- after config is complete, it sends `FromRadio.config_complete_id`
- only then does it move to `STATE_SEND_PACKETS`

### Special nonces

`PhoneAPI.h` defines:

- `SPECIAL_NONCE_ONLY_CONFIG = 69420`
- `SPECIAL_NONCE_ONLY_NODES = 69421`

Meaning:

- `69420`: send config-related state without full node DB walk
- `69421`: focus on node info flow

Official Apple app uses the same constants in `AccessoryManager.swift`.

## `ToRadio` Variants

Source of truth: `PhoneAPI.cpp`, `PacketAPI.cpp`.

Officially handled variants include:

- `packet`
- `want_config_id`
- `disconnect`
- `xmodemPacket`
- `mqttClientProxyMessage`
- `heartbeat`

### `ToRadio.packet`

This is the normal way for the app to send a mesh packet through the connected device.

Flow:

1. phone builds `MeshPacket`
2. wraps in `ToRadio.packet`
3. device `PhoneAPI::handleToRadioPacket()`
4. device applies local rules and rate limits
5. device calls `service->handleToRadio(p)`
6. device injects it into mesh routing via `MeshService`

### `ToRadio.want_config_id`

Starts config sync.

The response is not a single packet. It is the whole config state machine ending with:

- `FromRadio.config_complete_id = same nonce`

### `ToRadio.heartbeat`

In `PhoneAPI.cpp`, heartbeat only sets a flag:

- `heartbeatReceived = true`

Then the next `getFromRadio()` emits:

- `FromRadio.queueStatus`

So on modern firmware, heartbeat is effectively a "please prove you are alive and tell me queue status" request.

Official Apple app uses this to detect link liveness.

## `FromRadio` Variants

`PhoneAPI.cpp` sends these categories:

- config-related data: `my_info`, `node_info`, `metadata`, `channel`, `config`, `moduleConfig`, `fileInfo`, `config_complete_id`
- steady-state data: `packet`, `queueStatus`, `mqttClientProxyMessage`, `clientNotification`, `xmodemPacket`
- system events: `rebooted`, `log_record`

Important distinction:

- `FromRadio.packet` carries a `MeshPacket`
- `FromRadio.queueStatus` is not a mesh packet
- `FromRadio.clientNotification` is not a mesh packet

That distinction matters because official apps treat them differently.

## `MeshPacket` Field Semantics

Source of truth: `mesh.pb.h`, `Router.cpp`, `MeshModule.cpp`, app code.

### `MeshPacket.id`

This is the packet identifier for the mesh packet itself.

Important upstream comments say:

- it is unique per sender for a short time window
- used by flooding / ACK / retransmission logic
- used by crypto implementation too

In firmware:

- if phone did not set `id`, `MeshService::handleToRadio()` generates one
- queue-status responses use the packet's `id` as `mesh_packet_id`

Therefore:

- app-created packet IDs matter
- if app sets `id`, later status signals should correlate back to this same ID

### `decoded.request_id`

Upstream protobuf comment:

- only used in routing or response messages
- indicates the original message ID this message is reporting on

In practice:

- routing ACK/NAK packets use `decoded.request_id = original_packet.id`
- normal responses to a request also use `request_id` to point at the request packet
- official apps use `request_id` to correlate a response/ACK with the outbound request

### `decoded.reply_id`

Upstream protobuf comment:

- indicates this message is a reply to a previous message

This is user/content-level threading, not transport ACK.

Examples:

- text reply to a previous message
- emoji reaction targeting a prior message

Do not confuse `reply_id` with ACK state.

### `want_ack`

This means the sender wants reliable delivery behavior and an ACK-style confirmation path.

However, upstream code makes one critical exception:

- `Router.cpp` forcibly clears `want_ack` on broadcast packets before they go over LoRa

That means:

- broadcast over-the-air packets are never true "normal ACKed unicast sends"
- any phone/app logic that treats broadcast as awaiting a direct recipient ACK is wrong

## Broadcast vs Direct Message

This is the most important rule for current debugging.

### Direct message

For non-broadcast packets:

- `want_ack` can remain set
- `ReliableRouter` tracks retransmissions
- recipient may send a true routing ACK/NAK
- official apps can eventually move message to delivered/error based on routing result

### Broadcast message

For broadcast packets:

- `Router.cpp` clears `want_ack` before air transmission
- no normal destination-specific ACK flood is used
- reliability is based on rebroadcast observation and implicit acknowledgment logic

Upstream `ReliableRouter.cpp` behavior:

- if the original sender sees someone rebroadcast its broadcast packet
- firmware generates an implicit ACK internally
- this is an optimization for flooding reliability
- that ACK is generated on the original sender node and then surfaces to the phone as a local `ROUTING_APP` result tied to the original `request_id`
- it should not be rewritten as if it came from the rebroadcaster's node identity

This implicit ACK is not the same thing as:

- a direct-message ACK from the intended peer
- a conversation-level proof that one specific remote user acknowledged receipt

Therefore:

- a broadcast packet must not be surfaced to phone UI as "waiting for DM ACK from peer X"
- a rebroadcaster or relay identity must not be mistaken for the final application peer

## Official ACK / NAK Generation

### Routing ACK packet format

`MeshModule::allocAckNak()` builds a packet with:

- `decoded.portnum = ROUTING_APP`
- payload = encoded `Routing`
- `decoded.request_id = original packet id`
- `to = original sender`

This is the canonical ACK/NAK message shape.

### Reply packet format

`setReplyTo()` in `MeshModule.cpp` sets:

- `p->to = original sender`
- `p->channel = original channel`
- `p->want_ack = to.want_ack` except local-phone case
- `p->decoded.request_id = original request id`

So for admin or other request/response flows:

- an ordinary response packet can also carry `request_id`
- apps may use that to match request -> response even when it is not a routing ACK

### ReliableRouter rules

`ReliableRouter.cpp` distinguishes:

- ACK: routing packet with `error_reason == NONE`, or non-routing response carrying `request_id`
- NAK: routing packet with non-`NONE` error reason

Key code:

- `ackId = ((c && c->error_reason == NONE) || !c) ? p->decoded.request_id : 0`
- `nakId = (c && c->error_reason != NONE) ? p->decoded.request_id : 0`

Meaning:

- a packet with `request_id` can stop retransmission
- for routing packets, the error code decides ACK vs NAK

## `QueueStatus` Semantics

This is the second most important rule.

Source of truth:

- `MeshService::sendToMesh()`
- `MeshService::sendQueueStatusToPhone()`
- `PhoneAPI::handleToRadioPacket()`
- Android `PacketHandler.handleQueueStatus()`

### What `QueueStatus` means

After a phone-originated mesh packet is handed into routing, firmware always tries to send a `QueueStatus` back to the phone:

- `res` = immediate result of enqueue / local send attempt
- `free` = current number of free queue entries
- `maxlen` = queue capacity
- `mesh_packet_id` = the outbound packet ID this status refers to

This happens in `MeshService::sendToMesh()`.

So `QueueStatus` answers:

- was this packet accepted by the local device/radio path?
- what is the local transmit queue state right now?

It does not answer:

- whether the remote node received it
- whether the remote node ACKed it
- whether a routing error happened later

### Heartbeat `QueueStatus`

Heartbeat also returns a `QueueStatus`, but that one is only link-liveness / local queue information.

It is not a send result for a specific message unless `mesh_packet_id` points to one.

### Official Android interpretation

`PacketHandler.kt`:

- when packet is sent to radio, status becomes `ENROUTE`
- `handleQueueStatus()` only completes the local "radio accepted it" wait
- if `requestId != 0`, Android matches by `mesh_packet_id`

So Android uses `QueueStatus` to move past the radio-send stage, not to declare final delivery.

This is the exact reason "queueStatus arrived" is not enough to clear "waiting to be acknowledged".

## Official App Send-State Interpretation

### Android

Relevant code:

- `PacketHandler.kt`
- `MeshDataHandler.kt`
- `DataPacket.kt`

Android status model:

- `QUEUED`
- `ENROUTE`
- `DELIVERED`
- `ERROR`
- `RECEIVED`

Behavior:

1. app sends packet -> `ENROUTE`
2. firmware returns `QueueStatus(mesh_packet_id=id)` -> local send gate completes
3. later, `ROUTING_APP` packet with `request_id=id` drives final status

`MeshDataHandler.handleRouting()`:

- decodes `Routing`
- calls `handleAckNak(requestId, fromId, routingError, relayNode)`

Status mapping:

- ACK from ultimate target or reaction target may become `RECEIVED`
- ACK otherwise becomes `DELIVERED`
- non-zero routing error becomes `ERROR`

The essential point:

- final delivery status comes from `ROUTING_APP`, not `QueueStatus`

### Apple

Relevant code:

- `AccessoryManager.swift`
- `MeshPackets.swift`

Apple receives `FromRadio.packet`, checks `decoded.portnum`, and for `ROUTING_APP` calls:

- `MeshPackets.shared.routingPacket(packet:connectedNodeNum:)`

That handler:

- finds message by `packet.decoded.requestID`
- stores `ackError`
- if `routingMessage.errorReason == .none`, sets `receivedACK = true`
- records `relayNode`, `ackTimestamp`, `ackSNR`

Apple therefore also treats:

- routing packet keyed by `requestID`
- as the authoritative ACK/NAK path

Again:

- `QueueStatus` is not final delivery

## BLE Read / Notify Contract

Combining firmware and Apple app:

1. phone writes `ToRadio` to `TORADIO`
2. firmware eventually increments `fromNum`
3. firmware notifies `FROMNUM`
4. app starts draining `FROMRADIO`
5. app keeps reading until empty read

Important consequences:

- if firmware queues `FromRadio` data but does not cause the app to drain, status updates can appear delayed
- if firmware only wakes the app for some variants and not others, phone-side state may lag
- if `QueueStatus` or `ROUTING_APP` packets are generated but not drained, UI stays stale

## What Must Not Be Misinterpreted

### Rule 1: `QueueStatus` is not final ACK

Incorrect:

- "I saw `QueueStatus` for packet `X`, so message `X` was acknowledged by the peer"

Correct:

- "`QueueStatus` means the local radio path accepted or rejected the outbound packet"

### Rule 2: `reply_id` is not ACK state

Incorrect:

- "This packet has `reply_id`, so it acknowledges the earlier packet"

Correct:

- `reply_id` is conversation-level reply threading

### Rule 3: broadcast should not be modeled as direct-message ACK

Incorrect:

- "Broadcast packet to `0xFFFFFFFF` should wait for recipient ACK"

Correct:

- broadcast ACK-on-air is suppressed by upstream router
- reliability uses flooding / rebroadcast observation
- relay observations are not direct recipient ACK semantics

### Rule 4: relay / rebroadcast node is not automatically the logical sender of ACK

Incorrect:

- "I saw a routing-related event from short relay `0x11`; therefore node `0x11` is the chat peer who acknowledged"

Correct:

- it may be an intermediate relay, rebroadcaster, or broadcast-side routing artifact
- interpretation depends on whether the original packet was unicast or broadcast

## Concrete Rules For Our Integration

These rules follow upstream behavior and should be treated as protocol constraints.

### Outbound phone message

- Preserve `MeshPacket.id` if app/core assigned one.
- Use that same ID as the stable correlation key across:
  - `QueueStatus.mesh_packet_id`
  - `ROUTING_APP.decoded.request_id`
  - any response packet carrying `decoded.request_id`

### Broadcast text send

- Do not model broadcast text as requiring a direct recipient ACK.
- Do not convert relay or rebroadcast observations into peer-delivery ACK for chat UI.
- Do not surface a broadcast routing artifact as if it were a DM acknowledgment from a user node.

### Direct-message text send

- `QueueStatus` means local acceptance only.
- Wait for `ROUTING_APP` or a request-correlated response to decide final state.
- A `Routing.Error.NONE` for matching `request_id` is the canonical success signal.

### Admin / request-response flows

- Some requests may be effectively confirmed by a real response packet carrying `request_id`.
- Official Apple app explicitly treats admin responses as an ACK-equivalent for the admin log entry.

### BLE transport

- `FROMNUM` must wake draining of `FROMRADIO`.
- All generated `FromRadio` packets that matter to UI state must be drainable in a timely way.

## Why `from=00000011` Was Suspicious In Our Case

From upstream rules alone:

- if the original outbound packet was a broadcast message
- and the phone/UI later treated a routing-related event from `0x00000011` as the final peer ACK
- that interpretation is wrong

Because upstream says:

- broadcasts do not carry normal over-air `want_ack`
- their reliability path is based on flooding and implicit observation
- relay/rebroadcast evidence is not equivalent to DM recipient acknowledgment

So if a broadcast text on our branch ended up surfacing:

- `request_id = original text id`
- plus a routing-style success attributed to a relay-like node

the likely bug is not "Meshtastic protocol says relay `0x11` is the peer ACK sender".

The likely bug is:

- our integration mapped a broadcast-side routing observation into a DM-style ACK event for the phone layer

That conclusion is source-consistent with upstream behavior.

## Practical Debug Checklist

When debugging a message stuck on "waiting to be acknowledged", check in this order:

1. Did the phone send `ToRadio.packet` with a stable `MeshPacket.id`?
2. Did firmware emit `QueueStatus.mesh_packet_id == that id`?
3. If no, the problem is local enqueue / transport / BLE drain.
4. If yes, did a later `FromRadio.packet` arrive with `decoded.request_id == that id`?
5. If yes and `portnum == ROUTING_APP`, decode `Routing.error_reason`.
6. If the original packet was broadcast, do not interpret relay observations as DM ACK.
7. If the original packet was a request expecting content response, also check non-routing response packets carrying `request_id`.

## Short Reference Table

`MeshPacket.id`

- ID of the outbound packet itself
- primary correlation key

`QueueStatus.mesh_packet_id`

- local enqueue/send result for outbound packet ID
- not final remote ACK

`decoded.request_id`

- "this packet refers to original packet ID X"
- used for routing ACK/NAK and normal responses

`decoded.reply_id`

- content/thread reply to previous message
- not transport ACK

`want_ack`

- reliable-delivery request for unicast path
- cleared by router for broadcast over the air

`ROUTING_APP`

- canonical ACK/NAK packet family
- official apps use it for final delivery state

## Notes For Future Maintenance

If upstream changes behavior, re-check at least:

- `PhoneAPI.cpp`
- `Router.cpp`
- `ReliableRouter.cpp`
- `MeshService.cpp`
- Apple `BLEConnection.swift`
- Apple `MeshPackets.swift`
- Android `PacketHandler.kt`
- Android `MeshDataHandler.kt`

If we change our local adapter behavior, we should compare against this document first, then update the implementation, not the rules.
