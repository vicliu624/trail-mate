# Paired Linux mesh validation — 2026-11-08

This record extends the earlier [Meshtastic qualification](linux-paired-meshtastic-validation-20261108.md).
Final installed versions are Zero `1.0.1+cardputer.20261108.20` (ARM64) and CM0
`1.0.1+mesh.20261108.10` (ARMHF). Both package builds and SHA-256-verified
installations completed successfully.

Package SHA-256:

- Zero `.20`: `1c8ae5c52be5b6c434ffedb637b0029e846c0b08c261940e68332b4a8a6a9437`.
- CM0 `.10`: `c9ca418f6c6cae24c162ad719b8874ceef2ab39a6c923cad9a41a5ae6b84c5bf`.
Both devices were physically in the same room, approximately 1–3 m apart.
The Cardputer Zero uses its SX1262 Cap and framebuffer UI; T-Display-CM0 uses
its LR2021 through normal RadioLib packet operations. Mesh qualification did
not use SDR/IQ streaming or the custom SDR interrupt interface.

The base checkout is `8bc50ffc22aab1ea2c88472c9dcc3050e61590d4`, including the
preserved working changes. HTTPS `ls-remote` confirmed the same upstream main
revision after an SSH fetch transport failure. Device identities and private
keys remained on their devices.

## Radio and protocol evidence

| Protocol | Air configuration | Physical result |
| --- | --- | --- |
| Meshtastic | CN, LongFast, slot 35, 478.875 MHz, BW250, SF11, CR4/5, sync2B, default primary hash08 | Paired bidirectional private text and remote delivery ACKs passed. Both devices also decoded official firmware broadcasts. |
| MeshCore | 869.525 MHz, BW250, SF11, CR4/5, sync12, preamble16 | Signed identity discovery, bidirectional private text and remote delivery ACKs passed. |
| Reticulum | LoRa only, 869.525 MHz, BW125, SF9, CR4/5, sync12, preamble18 | Signed Announce discovery, bidirectional LXMF private text, fragmented longer text and delivery proofs passed without IP transport. |

MeshCore identities were Zero `!95D5541B` and CM0 `!466B5FAE`. The private
texts `mc cm0 zero tm3` and `mc zero cm0 tm3` were received on the opposite
device and their originating rows reached Delivered. Protocol message IDs
differ between the MeshCore local and receiving ledgers and are not compared
as if they were identical wire identifiers.

For Reticulum, CM0's `rt cm0 zero tm3` reached Delivered, and Zero's longer
`rt zero cm0 tm3 ...` reached CM0 and obtained a delivery proof. Physical
keyboard automation reordered part of the longer draft's digit suffix; the
claim is delivery of the actual recorded text, not equivalence to the intended
keystroke sequence. Full Reticulum destination identities were used for sends.

## Official Meshtastic interoperability

The user confirmed receipt in the official client of both broadcasts:

- CM0: `cm0 official cn tm4`, packet `AA3D67B2`, sent at 20:18:01 CST.
- Zero: `zero official cn tm4`, packet `5EA5ABC1`, sent at 20:19:57 CST.

Official peers `!A1B3B57C` and `!D917FE08` sent readable broadcasts and NodeInfo.
Repeated packet `9B2B8422` was decoded multiple times and stored once on CM0.
After the storage repair, Zero stored the official broadcast `Bye`, packet
`2651561A`, and the user confirmed an audible notification. That packet was
addressed to `FFFFFFFF`; it is not evidence of an incoming private exchange.

Zero's first new PKI private attempt to `!A1B3B57C`, packet `5EA5ABC2`, received
an authenticated, addressed `NO_CHANNEL` NACK. It was marked Failed. Its cached
peer public key was older than the later received NodeInfo key. Linux omitted
the existing shared key-resynchronization policy already used by ESP32/nRF52.
The Linux routing handler now executes that policy on a matched `NO_CHANNEL`
or `PKI_UNKNOWN_PUBKEY` NACK. It consumes the pending ACK first so a duplicate
NACK cannot repeat the NodeInfo request.

At 20:37:22 CST, Zero received fresh official NodeInfo and updated the peer key.
The next private text, `zero private official tm6`, packet `6D697301`, was
transmitted at 20:39:54 and acknowledged by `!A1B3B57C` at 20:39:56. The persisted
row reached Delivered, and the user confirmed the official client displayed it.
At 20:40:29, the official peer sent private `Hi`, packet `48CBBE29`, addressed
to `!4BE5DDEF`. Zero decrypted and stored it once and transmitted the Routing
ACK at 20:40:30. The live conversation showed both the Delivered outbound text
and the incoming `Hi` without reopening the conversation.

After the final `.20` installation, restart qualification and restoration of
the normal launcher/settings, Zero sent `zero private official tm7`, packet
`DAC68999`. The official peer ACK again produced a persisted Delivered row.
The final database contained 16 intact message rows. The final framebuffer
capture showed the received `Hi` and this newly Delivered message together.

The shared protobuf encoder also set `want_response` while writing a present
`bitfield` with bit 1 clear. NodeInfo and application-data requests now encode
the response-request bit consistently with the existing ESP32/nRF52 adapters.
MQTT permission remains clear. A serialized-byte regression failed against the
old encoder and passed after the fix, covering both boolean values and reused
caller-owned scratch. The paired Linux NACK/ACK regression also passed.

## Storage defect and acceptance

Zero's old database became malformed during the session. A configuration
record contained bytes from a chat message and subsequently failed startup
validation. The complete damaged database and recovery artifacts were retained
privately on the device. A verified device-local baseline was installed, with
three actually received official broadcasts reconstructed from packet logs.
Some temporary test history was not recovered. The user explicitly waived
further historical recovery and requires healthy storage after the repair.

An independent, deterministic regression exposed a Linux identity preflight
bug: opening and closing the SQLite inode directly removed the process's
existing POSIX database locks. The new check opens the database through SQLite,
then verifies ownership and sets private permissions through a pinned `O_PATH`
handle. Closing that handle preserves the SQLite locks. The old implementation
failed the lock regression; the new implementation passes. This establishes a
specific unsafe locking defect, without claiming forensic proof that it was the
only possible cause of the damaged database.

The local X25519 pair and the device, MeshCore and Reticulum identities were
compared against the device-local evidence and retained unchanged. Private keys
and complete identity databases were not downloaded.

Validation completed so far:

- Identity round trip, file protection and replacement regressions passed.
- SQLite reserved-lock preservation passed, including the failure path when
  another connection already owns the writer transaction.
- A 128-iteration stress test concurrently writes 4 KiB configuration records,
  writes 128 messages on another SQLite connection, and opens 128 identity
  instances. Integrity, every message, the complete final configuration buffer
  and the unchanged identity public key passed.
- Long-chat/configuration/protocol round trips passed on the native host and
  the cross-compiled test executed on the actual Zero.
- Subsequent fresh broadcasts and the official `Bye` were stored on Zero, and
  its database integrity check returned `ok`.

The cross-compiled reserved-lock and concurrent-storage tests subsequently ran
on the actual Zero and passed. CM0 completed three graceful restarts with
integrity `ok`, all 20 original message rows preserved and unchanged identities.
Zero then completed three startups, with graceful restarts between them, on
the final package. All 15 message rows and their statuses, including the
official private message and Delivered outbound record, survived unchanged.
All identities remained unchanged and every integrity check returned `ok`.
The normal APPLaunch service was restored and used to open the final version.
Both final running databases passed another read-only audit: configuration
length 4564 bytes, valid magic/version and Meshtastic protocol value 1.

## UI, sleep and automatic replies

Zero's topbar now reserves a complete compact status area, uses one line for
the conversation title with an ellipsis, and displays a compact help hint.
Actual screenshots show the full battery percentage beside a long peer name.
The default All inbox includes broadcasts. A new broadcast updated the preview
and unread count while the same message-list screen stayed open.

The framebuffer composition now connects the asynchronous backlight worker to
the shared screen state machine. Its first wake input is consumed; a subsequent
Enter/Space resumes the previous screen. Shutdown restores the saved brightness.
The native integration regression and actual backlight-off/wake checks passed.

Automatic reply remains sleep-only. A shared incoming-message observer is used
by Linux and the ESP32 wrapper. Actual Reticulum tests demonstrated:

- Zero awake: received `rt awake no reply tm3`, no automatic reply.
- Zero asleep with backlight zero: received `rt asleep auto tm3`, sent
  `zero asleep tm3`, and obtained Delivered.
- CM0 asleep with its compositor output off: received `rt cm0 sleep test tm3`,
  sent `cm0 asleep tm3`, and obtained Delivered.

The temporary enabled/text and Always On timeout values were restored to the
exact original database records. Both final running devices passed a readback
comparison against their device-local pre-test settings. Automatic reply is
left disabled as originally configured, with no test reply text; enabling it
continues to use the sleep-only policy.

## Cross-platform scope

The valid-frame/rearm fixes were checked against the ESP-IDF radio pump. Packet
IRQ progress must not clear a frame before `readData`; damaged frames are
rejected, and a successfully read frame survives failure to arm the next receive.
A shared normal-radio standby/startReceive helper is used for LR2021 restart.
No SDR or IQ capture API is called by these mesh paths.

The Reticulum contact-menu capability correction and automatic-reply observer
are shared UI/business changes. The SQLite descriptor fix is Linux-specific.
ESP32 already used the shared Meshtastic NACK resynchronization policy that is
now connected to Linux. Host production-code regressions and the ESP stack
hygiene check passed. No full ESP32 firmware build or physical ESP32 qualification
is claimed by this record.

All started package builds and focused regressions reached terminal success.
The final read-only audits reported no CRC, header, timeout, invalid-frame or
read errors in either device's current Meshtastic radio counters. Both devices
remain in CN / LongFast with their normal identities and profiles.
