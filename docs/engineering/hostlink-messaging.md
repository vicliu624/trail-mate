# HostLink messaging

The USB messaging API shares HostLink v1 framing, the existing management
session, and the device's ChatService and Team chat command. It does not create
a second radio runtime or message-body database. The browser adapter is
`site/device/src/chat-client.js`; `tools/hostlink/chat.mjs` uses the same adapter
through a Web Serial compatible PySerial transport.

## Supported operations

Require management capabilities operation 0 to advertise the operation being
used. These additions do not change legacy commands, ACKs, or event layouts.

| Operation | Request body | Response body |
| --- | --- | --- |
| 112 ChatCapabilities | `[]` | `[1, operation_store16, context16, protocol, max_text_bytes, metadata_slots, features, team_or_nil]` |
| 113 ChatSend | `[[operation_store16, context16], operation_id16, target, text]` | `receipt` |
| 114 ChatSendGet | `[operation_store16, original_owner16, operation_id16]` | `[lookup, receipt_or_nil]` |

`target` is `[protocol, channel, kind, full_identity_bytes]`:

- Kind 0: a Meshtastic or MeshCore channel, with an empty binary identity.
- Kind 1: Meshtastic direct peer with its four-byte node ID in network byte
  order, or Reticulum direct destination with the full 16-byte destination hash.
  MeshCore direct addressing is not advertised by this version.
- Kind 2: Team text, with the full eight-byte active Team ID and channel 2,
  matching the existing Team UI command adapter. It never becomes a core chat
  channel send. The shared Team command installs existing keys, sends the Team
  message, and appends the local Team chat log.

The current maximum text is 256 UTF-8 bytes; radio-specific business rules can
still reject a shorter protocol limit. Empty text, NUL, invalid UTF-8, zero IDs,
short identities, and unsupported targets are rejected. No display name or
short destination hash is used to choose a recipient. Protocol values remain
1 Meshtastic, 2 MeshCore, and 4 Reticulum. Legacy protocol 3 is not a new target.

The optional Team row is `[team_id8, security_round, 2]`; absence means no active
keyed Team was available for this projection. Feature mask 15 means bounded
submission receipts, existing live text events, existing live app events, and
shared Team text submission. It does not advertise contact/history queries,
offline receive replay, mark-read, or a lossless management event stream.

`context16` changes after reboot and whenever the self node, selected protocol,
radio/channel configuration, active Team ID/round, or keyed-Team readiness
changes. Stale contexts cannot authorise new sends. The operation store UUID
is separate from chat-body storage: it survives normal reboot in the dedicated
`hl_chat` NVS namespace and changes only if that namespace is cleared. It is
not a message-history generation or an author credential.

## Submission, delivery, and recovery

A receipt is:

```
[operation_id16, original_owner16, text_sha256_32, submission,
 message_ref16_or_nil, delivery, revision, failure]
```

Submission values are 0 Reserved, 1 Submitted, 2 Rejected, 3 Uncertain. Delivery
values are 0 Unknown, 1 Queued, 2 Sent, 3 Delivered, 4 Failed. They describe
different facts. In particular, a frame ACK proves transport acceptance, and a
Submitted receipt proves business submission; neither proves remote delivery.
Team currently has no remote delivery proof and reports its known queue
admission only. The legacy EV_TX_RESULT boolean also does not prove delivery.

The opaque message reference is a receipt-local correlation value; its first
four bytes contain the business message ID in little-endian order. It is not
an API for looking up arbitrary messages, or a globally unique message ID.
For core chat, lookup refreshes delivery only if protocol, target, text length,
and SHA-256 still match the existing ChatService message. Missing or mismatched
messages produce Unknown instead of attributing a colliding ID's status.
Failure values below 0x4000 come from MeshOperationFailure; 0x4000 marks a shared
UiActionFailure, and 0x8000 marks a management admission failure.

Before calling the business sender, the device commits a metadata reservation
to NVS. The existing app loop advances the owner; it finishes accepted work
even when USB disconnects. No send is repeated by the owner. The body remains
in the existing chat store only; the reservation contains its hash and target.
If reset happens around the business call, an old Reserved record is exposed
as Uncertain and is never automatically executed again. A failed or ambiguous
reservation write does not invoke the business sender.

There are 16 metadata slots. Submitted/Rejected records may be replaced by
newer submissions; Reserved/Uncertain records are never evicted to make room.
If all slots are unresolved, new sends return Busy before transmission.
Lookup 0 means a retained receipt. Lookup 1 with nil means history unavailable,
including eviction or absence: it must never be translated to "not sent" or
used to trigger automatic resend. Corrupt or unreadable storage fails closed.
This bounded retention is not an exactly-once guarantee after metadata expiry.

The client persists the intent and then an attempted fence **before** writing
the send request. After timeout, disconnect, or restart, it performs only
ChatSendGet using the original owner and operation. It first checks the
operation store UUID, so another device cannot receive an old pending send.
It verifies receipt IDs and the text hash. A missing receipt requires an
explicit human decision to create a new intent. Settings, Geocaching, routes,
and chat can share a connection/session; closing ChatClient does not close it.

## Receive events

The client observes the existing connection rather than consuming the device
UI's event queue. It decodes EV_RX_MSG and EV_TX_RESULT, passes Team state bytes,
and assembles EV_APP_DATA (including authenticated/decrypted Team traffic).
Team port 303 additionally exposes the existing Team chat header and text as
`event.teamChat`; location/command payload bytes are retained for their codecs.

The assembler is bounded to four simultaneous bodies and 16 KiB each, with a
five-second lifetime. Chunked bodies require a packet ID and consecutive,
consistent offsets. Missing/overlapping/conflicting chunks report a receive
error rather than producing a mixed message. Disconnect clears partial bodies.
These live legacy events remain best effort: this version does not promise
receive history across disconnects or oversized legacy text frames. Consumers
must not present a connected USB link as evidence of a complete transcript.

## Host usage

Install repository JavaScript dependencies and PySerial first:

```
npm ci --ignore-scripts --prefix site
python -m pip install pyserial
node tools/hostlink/chat.mjs COM25 --listen 20
node tools/hostlink/chat.mjs COM25 --team --send "Team USB check" --intent ./team-check.json --listen 20
node tools/hostlink/chat.mjs COM25 --recover ./team-check.json --listen 5
```

Use `--python PATH` when the default Python is not the installed interpreter.
The send command requires a new intent file. An existing path is rejected;
recovery never transmits. Intent updates use a flushed temporary file and
replacement. The tool closes the serial port in its finally path. Intents
contain the requested text and public target/correlation metadata, never Team
PSKs or private radio keys. Treat them as local chat data.

## ESP resource ownership

The runtime is allocated in PSRAM on first use; lack of PSRAM returns Busy.
Only a pointer is present during ordinary device use. The command owner has
one bounded text buffer, fixed metadata scratch, and one NVS handle. It uses
the existing command pump and response arena, with no new task, task stack,
unbounded queue, or ordinary startup hydration work. Bootloader transition
returns Busy while an accepted chat operation is still being committed.

Verification includes native fault injection of the actual ESP adapter with
atomic-NVS and USB test ports, the real shared Team chat action, portable
journal/reboot/capacity tests, and browser no-retry/receive/correlation tests.
The native hash double tests ownership and matching only; firmware uses the
existing mbedTLS SHA-256 compatibility layer.

### Pager verification, 2026-11-12

The `tlora_pager_sx1262` application built from `cc738eee` completed with
92,616 bytes static RAM and 5,216,001 bytes reported Flash. Static RAM increased
by eight bytes relative to the accepted Contacts recovery image. The runtime
uses lazy PSRAM storage and the existing app loop. The retained application
image SHA-256 is
`5a0904b4a3eeee95cc60834d7dfa68605210a3dfeae022085004c2f77ebea1fe`.

The actual native USB CDC device accepted one Team text and one primary-channel
text using the shared JavaScript client. A fresh connection recovered the
original Team receipt. After a device restart, the operation store UUID stayed
the same, the context changed, and both original operations were recovered
using their original owners and IDs. The Team receipt retained queue admission;
the ordinary chat receipt refreshed to Sent from ChatService. The original
Team, security round, leader role, and three-member roster were preserved.
Every host process closed the serial port after its bounded operation.

This hardware record establishes USB submission and durable recovery. The
receive decoders and chunk fault paths are exercised by protocol fixtures;
remote recipient delivery requires the protocol's corresponding proof. The
user waived further Pager-to-Linux radio acceptance runs for this change.

The final management-window and SD-exclusive-owner hardening image,
`7b2a62e3`, subsequently built with the same 92,616 static RAM bytes and
5,216,049 reported Flash bytes. Its SHA-256 is
`831b38f2f9e1e9c337bf83f70c3c38c1cf4539e706da1ac291f5c265e2d37110`.
It was installed without erasing NVS or SD. Both original receipts were again
recovered with their original IDs/owners, and the original Team/round/roster
were retained. The host client's advertised-operation gate was also exercised
against this device. Disassembly of the verified application shows a 1,680-byte
lazy PSRAM runtime allocation; the messaging command frame uses the existing
task stack and does not create another task or stack.
