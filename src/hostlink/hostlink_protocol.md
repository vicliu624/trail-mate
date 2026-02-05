# HostLink Protocol (TrailMate)

This document describes the HostLink wire protocol used by the Data Exchange
mode. It is intentionally self-contained so a PC-side client can implement the
protocol by reading only this folder.

## Transport
- USB CDC-ACM (virtual serial)
- Binary frames (not line-based)
- Little-endian for all multi-byte fields

## Frame Format

All frames use the following layout:

```
0   1   2    3     4-5     6-7     8..(8+len-1)  (end-2..end-1)
HL  VER TYPE SEQ   LEN     PAYLOAD CRC16-CCITT
```

Fields:
- `Magic`: 2 bytes: `0x48 0x4C` ("HL")
- `VER`: protocol version (currently `0x01`)
- `TYPE`: frame type (see table below)
- `SEQ`: uint16, little-endian, sequence number
- `LEN`: uint16, little-endian, payload length
- `PAYLOAD`: `LEN` bytes
- `CRC16`: CRC-CCITT (poly `0x1021`, init `0xFFFF`) over header+payload
  (from Magic to end of payload). CRC is appended little-endian.

Constraints:
- Max payload length: 512 bytes (`kMaxFrameLen`).
- Frames with bad CRC or invalid version are dropped.

## Frame Types

| Type | Name           | Dir | Description |
|------|----------------|-----|-------------|
| 0x01 | HELLO          | PC→Dev | Start handshake |
| 0x02 | HELLO_ACK      | Dev→PC | Handshake response |
| 0x03 | ACK            | Dev→PC | Command ack + status code |
| 0x10 | CMD_TX_MSG     | PC→Dev | Send a text message |
| 0x11 | CMD_GET_CONFIG | PC→Dev | Read current config/status |
| 0x12 | CMD_SET_CONFIG | PC→Dev | Set config values (TLV) |
| 0x13 | CMD_SET_TIME   | PC→Dev | Set device epoch time |
| 0x14 | CMD_GET_GPS    | PC→Dev | Request current GPS snapshot |
| 0x80 | EV_RX_MSG      | Dev→PC | Device received a message |
| 0x81 | EV_TX_RESULT   | Dev→PC | Send result for CMD_TX_MSG |
| 0x82 | EV_STATUS      | Dev→PC | Device status/config (TLV) |
| 0x83 | EV_LOG         | Dev→PC | Optional log event (reserved) |
| 0x84 | EV_GPS         | Dev→PC | GPS snapshot (scaled ints) |
| 0x85 | EV_APP_DATA    | Dev→PC | App payload (decrypted), chunked |

## Error Codes (ACK payload)

| Code | Name          |
|------|---------------|
| 0    | OK            |
| 1    | BAD_CRC       |
| 2    | UNSUPPORTED   |
| 3    | BUSY          |
| 4    | INVALID_PARAM |
| 5    | NOT_IN_MODE   |
| 6    | INTERNAL      |

## Capabilities (HELLO_ACK)

Bitmask (uint32):
- bit0: `CapTxMsg`
- bit1: `CapConfig`
- bit2: `CapSetTime`
- bit3: `CapStatus`
- bit4: `CapLogs`
- bit5: `CapGps`
- bit6: `CapAppData`

## Handshake

1) PC opens CDC and asserts DTR.
2) PC sends HELLO.
3) Device replies HELLO_ACK.
4) Link state becomes READY. Commands are accepted.

If no HELLO is received within 5 seconds after DTR, the device returns to
Waiting state. Any non-HELLO frames before READY receive `ACK(NOT_IN_MODE)`.

## Command Payloads

### HELLO (0x01)
Payload: empty.

### HELLO_ACK (0x02)
Payload:

```
u16  protocol_version
u16  max_frame_len
u32  capabilities
u8   model_len
u8[] model (ASCII)
u8   fw_len
u8[] fw_version (ASCII)
```

### ACK (0x03)
Payload:

```
u8 status_code
```

### CMD_TX_MSG (0x10)
Payload:

```
u32 to
u8  channel
u8  flags
u16 text_len
u8[] text (UTF-8)
```

### CMD_GET_CONFIG (0x11)
Payload: empty. Device replies with EV_STATUS.

### CMD_SET_CONFIG (0x12)
Payload is a TLV list:

```
u8 key, u8 len, u8[len] value
```

Config keys:
- 1: MeshProtocol (u8)
- 2: Region (u8)
- 3: Channel (u8)
- 4: DutyCycle (u8, 0/1)
- 5: ChannelUtil (u8)

### CMD_SET_TIME (0x13)
Payload:

```
u64 epoch_seconds
```

### CMD_GET_GPS (0x14)
Payload: empty. Device replies with EV_GPS.

## Event Payloads

### EV_RX_MSG (0x80)
Payload:

```
u32 msg_id
u32 from
u32 to
u8  channel
u32 timestamp
u16 text_len
u8[] text (UTF-8)
```

### EV_TX_RESULT (0x81)
Payload:

```
u32 msg_id
u8  success (0/1)
```

### EV_STATUS (0x82)
Payload is a TLV list:

Status keys:
- 1: Battery (u8, 0-100, 0xFF=unknown)
- 2: Charging (u8, 0/1)
- 3: LinkState (u8)
- 4: MeshProtocol (u8)
- 5: Region (u8)
- 6: Channel (u8)
- 7: DutyCycle (u8, 0/1)
- 8: ChannelUtil (u8)
- 9: LastError (u32)

LinkState values:
- 0: Stopped
- 1: Waiting
- 2: Connected (reserved)
- 3: Handshaking
- 4: Ready
- 5: Error

### EV_LOG (0x83)
Reserved for optional logs. Not emitted by default.

### EV_GPS (0x84)
Payload:

```
u8  flags
u8  satellites
u32 age_ms
i32 lat_e7
i32 lon_e7
i32 alt_cm
u16 speed_cms
u16 course_cdeg
```

Flags:
- bit0: valid fix
- bit1: has altitude
- bit2: has speed
- bit3: has course

Scaling:
- `lat_e7` / `lon_e7`: degrees * 1e7
- `alt_cm`: meters * 100
- `speed_cms`: m/s * 100
- `course_cdeg`: degrees * 100

If `valid fix` is 0, position fields should be ignored.

### EV_APP_DATA (0x85)
Payload (fixed header + chunk):

```
u32 portnum
u32 from
u32 to
u8  channel
u8  flags
u8  team_id[8]
u32 team_key_id
u32 timestamp_s
u32 total_len
u32 offset
u16 chunk_len
u8[] chunk
```

Flags:
- bit0: has team metadata
- bit1: want_response (from mesh)
- bit2: was_encrypted_on_air (best-effort)
- bit3: more_chunks

Notes:
- Payload is plaintext (decrypted).
- For Team apps (TEAM_MGMT/POS/WP/TRACK/CHAT), `team_id` and `team_key_id`
  are filled and `portnum` matches the Team app port.
- For TEAM_MGMT, `chunk` is the TeamMgmt wire format (version/type/payload).
- For non-team apps, `team_id`/`team_key_id` are zero.
- Chunking is used when payload size exceeds a single HostLink frame.
- `timestamp_s` is device uptime seconds when the payload was received.
- Max `chunk_len` is `kMaxFrameLen - header` (currently `512 - 40 = 472` bytes).
- Team portnums: 300=MGMT, 301=POSITION, 302=WAYPOINT, 303=CHAT, 304=TRACK.

## App Payload Coverage (All Messages)

HostLink forwards all app payloads through EV_APP_DATA. This section explains
how PC should decode each payload and which protobuf (if any) applies.

### 1) Team apps (portnum 300-304)

All Team payloads are decrypted by the device before EV_APP_DATA.
PC receives plaintext and must parse as below.
Team MGMT/CHAT/TRACK are custom binary. Team POSITION/WAYPOINT use protobuf.

#### TEAM_MGMT_APP (portnum 300)

Scenario: team formation, join, key distribution, status sync.

Payload is **TeamMgmt wire format**:

```
u8  version          // kTeamMgmtVersion = 1
u8  type             // TeamMgmtType
u16 reserved         // always 0
u16 payload_len
u8[] payload         // type-specific
```

TeamMgmtType:
```
1  Advertise
2  JoinRequest
3  JoinAccept
4  JoinConfirm
5  Status
6  Rotate (reserved, not used)
7  Leave  (reserved, not used)
8  Disband (reserved, not used)
9  JoinDecision
10 Kick
11 TransferLeader
12 KeyDist
```

Type-specific payloads (little-endian):

Advertise:
```
u8  team_id[8]
u16 flags (bit0 join_hint, bit1 channel_index, bit2 expires_at)
u32 join_hint       // if flag
u8  channel_index   // if flag
u64 expires_at      // if flag
u64 nonce
```

JoinRequest:
```
u8  team_id[8]
u16 flags (bit0 member_pub, bit1 capabilities)
u8  pub_len         // if member_pub
u8  pub[pub_len]    // if member_pub
u32 capabilities    // if capabilities
u64 nonce
```

JoinAccept:
```
u8  channel_index
u8  channel_psk_len
u8  channel_psk[channel_psk_len]
u32 key_id
u16 flags (bit0 has_params, bit1 has_team_id)
TeamParams          // if has_params
u8  team_id[8]      // if has_team_id
```

TeamParams:
```
u32 position_interval_ms
u8  precision_level
u32 flags
```

JoinConfirm:
```
u8  ok
u16 flags (bit0 capabilities, bit1 battery)
u32 capabilities    // if capabilities
u8  battery         // if battery (0-100)
```

JoinDecision:
```
u8  accept
u16 flags (bit0 reason)
u32 reason          // if reason
```

Kick:
```
u32 target
```

TransferLeader:
```
u32 target
```

KeyDist:
```
u8  team_id[8]
u32 key_id
u8  channel_psk_len
u8  channel_psk[channel_psk_len]
```

Status:
```
u8  member_list_hash[32]
u32 key_id
u16 flags (bit0 has_params)
TeamParams          // if has_params
```

How PC should respond:
- EV_APP_DATA itself requires no ACK.
- Mesh-level response is by sending the corresponding Team mgmt message
  (not implemented in HostLink yet). Typical flow:
  Advertise -> JoinRequest -> JoinAccept -> JoinConfirm -> Status.

#### TEAM_POSITION_APP (portnum 301)

Scenario: team member live position updates.

Payload is **meshtastic_Position protobuf** (Meshtastic schema).
See `mesh.pb.h` in `src/chat/infra/meshtastic/generated/meshtastic/`.
Key fields:
- latitude_i / longitude_i: degrees * 1e7
- altitude: meters
- timestamp: epoch seconds
- ground_speed: m/s
- ground_track: 0.01 degrees
- sats_in_view, fix_quality, fix_type, etc.

#### TEAM_WAYPOINT_APP (portnum 302)

Scenario: team waypoint sharing.

Payload is **meshtastic_Waypoint protobuf** (Meshtastic schema).
See `mesh.pb.h`.
Key fields:
- id
- latitude_i / longitude_i: degrees * 1e7 (optional)
- expire (epoch seconds)
- locked_to
- name (max 30 chars)
- description (max 100 chars)
- icon

#### TEAM_TRACK_APP (portnum 304)

Scenario: batched team track points (fixed interval).

Payload is **TeamTrackMessage** (custom binary):

```
u8  version         // kTeamTrackVersion = 1
u32 start_ts        // epoch seconds
u16 interval_s
u8  count           // <= 20
u32 valid_mask      // bit i -> point i valid
repeat count times:
  i32 lat_e7
  i32 lon_e7
```

Timestamp for point i = start_ts + i * interval_s.

#### TEAM_CHAT_APP (portnum 303)

Scenario: team chat messages (text/location/command).

Payload is **TeamChatMessage** (custom binary):

```
u8  version         // kTeamChatVersion = 1
u8  type            // 1=Text, 2=Location, 3=Command
u16 flags
u32 msg_id
u32 ts              // epoch seconds
u32 from            // node id
u8[] payload        // type-specific
```

Type-specific payloads:

Text:
```
u8[] text (UTF-8)   // length = total_len - header
```

Location:
```
i32 lat_e7
i32 lon_e7
i16 alt_m
u16 acc_m
u32 ts
u8  source
u16 label_len
u8  label[label_len]
```

Command:
```
u8  cmd_type        // 1=RallyTo, 2=MoveTo, 3=Hold
i32 lat_e7
i32 lon_e7
u16 radius_m
u8  priority
u16 note_len
u8  note[note_len]
```

### 2) Other app payloads (non-team portnum)

All other portnums are forwarded as EV_APP_DATA with plaintext payload
exactly as received from the mesh adapter. Most of these are **Meshtastic
protobuf messages**. PC should:

1) Read `portnum`.
2) Map it to a Meshtastic message type.
3) Decode protobuf using Meshtastic schemas.

Reference files in this repo:
- `src/chat/infra/meshtastic/generated/meshtastic/mesh.pb.h`
- `src/chat/infra/meshtastic/generated/meshtastic/portnums.pb.h`
- `src/chat/infra/meshtastic/generated/meshtastic/telemetry.pb.h`
- `docs/MESHTASTIC_PROTOBUF_USAGE.md`

Protobuf note:
- Protobuf is a binary schema-based serialization format (varint, length-delimited).
- Use Meshtastic schemas to decode payload bytes into structured fields.

Exceptions:
- TEXT_MESSAGE_APP (1) and TEXT_MESSAGE_COMPRESSED_APP (7) are delivered as
  EV_RX_MSG instead of EV_APP_DATA (the device already decompresses them).

NodeInfo:
- NODEINFO_APP (4) is forwarded as EV_APP_DATA.
- Payload is usually `meshtastic_User` (see `mesh.pb.h`), but some nodes send
  `meshtastic_NodeInfo`. PC should try `meshtastic_User` first, then fall back
  to `meshtastic_NodeInfo` if decode fails.

If a portnum is unknown, treat payload as opaque bytes.

## Example Frames (hex)

HELLO (seq=0x0001):
```
48 4C 01 01 01 00 00 00 3E 31
```

ACK OK (seq=0x0001, status=0):
```
48 4C 01 03 01 00 01 00 00 72 18
```

CMD_TX_MSG (seq=0x1234, to=0x01020304, channel=1, flags=0, text="hi"):
```
48 4C 01 10 34 12 0A 00 04 03 02 01 01 00 02 00 68 69 AB 45
```

## Notes
- All integers are little-endian.
- CRC16 uses CCITT-FALSE (init 0xFFFF, poly 0x1021).
- The device sends periodic EV_STATUS when READY.
- The device sends periodic EV_GPS when READY (default 1s cadence).
- The device forwards decrypted app payloads as EV_APP_DATA when available.
