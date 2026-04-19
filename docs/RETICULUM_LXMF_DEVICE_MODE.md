# Reticulum / LXMF Device Mode Plan

> Note
>
> This document captures the earlier phase-based device-mode plan and remains
> useful for historical context.
>
> The current authoritative implementation target is now documented in
> [RETICULUM_LXMF_RUNTIME_ALIGNMENT_PLAN.md](RETICULUM_LXMF_RUNTIME_ALIGNMENT_PLAN.md),
> which reflects the present codebase and the full runtime-alignment direction.

## Why this document exists

Trail Mate already has two very different things under the "RNode" umbrella:

- an honest low-level RNode air-layer implementation
- a USB CDC RNode KISS bridge so a real Reticulum host can use the device as a modem

What it does **not** yet have is a real device-side Reticulum/LXMF stack.

That distinction matters:

- `RNode modem` means the device forwards raw LoRa bytes and is controlled by an external Reticulum host
- `Reticulum/LXMF device mode` means the device itself owns identity, announces, destination discovery, packet encryption, message packing and local chat UX

The goal of this document is to define the end-state architecture and the first honest implementation slice. It intentionally avoids inventing a private "fake LXMF-like" chat envelope.

## Target outcome

Trail Mate should support these runtime roles:

- `Meshtastic`: native device-side chat over Meshtastic
- `MeshCore`: native device-side chat over MeshCore
- `LXMF`: native device-side chat over a Reticulum-compatible subset carried on RNode-style raw LoRa packets
- `RNode Bridge`: host-controlled modem mode for an external Reticulum/LXMF stack

In other words:

- `LXMF` is an application/network stack mode
- `RNode Bridge` is a modem/bridge mode

Today the codebase still couples "active radio backend" and "active user-visible protocol" into one selector. Because of that, the first implementation phase keeps `RNode Bridge` as a separate protocol value instead of fully moving it into the `Data Exchange` page. That split is deferred until the runtime can own more than one radio-facing backend safely.

## Constraints from the reference implementations

The local `.tmp/Reticulum` and `.tmp/LXMF` sources make the following points non-negotiable:

- Reticulum is a full network stack, not just a payload codec
- `Identity` uses two keypairs in practice:
  - Curve25519/X25519 for encryption and ECDH
  - Ed25519 for signatures
- destination addressing is derived from:
  - `name_hash = SHA256(app_name + aspects)[:10]`
  - `destination_hash = SHA256(name_hash + identity_hash)[:16]`
- opportunistic single-packet encryption uses:
  - ephemeral Curve25519 key exchange
  - HKDF-SHA256 derived 64-byte token key
  - AES-256-CBC + PKCS7
  - HMAC-SHA256 token authentication
- packet delivery proofs are real protocol behavior, not optional UI sugar
- LXMF wire format is:
  - 16 bytes destination hash
  - 16 bytes source hash
  - 64 bytes signature
  - msgpack payload
- LXMF routing above that depends on announces, destination recall, proofs, and later links/resources/propagation

## Honest phase boundary

The full Reticulum feature set is too large to land in one pass without high risk. The first device-side implementation therefore targets a **real interoperable subset**:

- local Reticulum identity storage
- `lxmf.delivery` destination derivation
- announce transmit/receive and signature validation
- peer discovery from direct announces
- opportunistic LXMF text delivery in single packets
- proof generation for received single-packet deliveries
- contact list integration from announces
- device-side unicast text chat to directly discovered peers

This first slice is intentionally **not** yet:

- multi-hop transport/path finding
- path request / path response handling
- link establishment
- resource transfer for large LXMF messages
- propagation nodes
- ticket/stamp enforcement
- ratchets
- full shared-instance parity with Python Reticulum

Even with those limits, the phase-1 result is still a real protocol subset:

- packet headers are real Reticulum packet headers
- announces are real Reticulum announces
- payload encryption is the real token scheme
- LXMF bodies are real LXMF bodies

## Proposed architecture

### 1. Radio carrier layer

Reuse the current RNode raw air layer:

- one-byte RNode air header
- packet fragmentation / reassembly
- raw LoRa payload send/receive
- radio parameter application

This remains the LoRa carrier for both:

- `RNode Bridge`
- `LXMF`

### 2. Reticulum core subset

Add a shared Reticulum wire layer with:

- constants for MTU, header sizing and hash sizes
- packet encode/decode for header-1 packets
- packet hash calculation
- destination hash helpers
- announce validation helpers
- HKDF + token encryption helpers

This layer must stay protocol-accurate and not depend on UI concerns.

### 3. LXMF wire layer

Add a shared LXMF wire layer with:

- minimal msgpack encoder/decoder for the LXMF structures used in phase 1
- peer announce app-data codec for `[display_name, stamp_cost]`
- text-message pack/unpack for `[timestamp, title, content, fields]`
- support for an optional fifth payload element so future stamp support does not break parsing

### 4. Platform identity layer

Add an ESP-side identity service that persists:

- Curve25519 public/private keypair
- Ed25519 public/private keypair
- local delivery destination hash

This layer should reuse the existing preferences/blob storage style already used by MeshCore identity handling.

### 5. LXMF adapter layer

Add a new `LxmfAdapter` that:

- composes the existing `RNodeAdapter` as its raw carrier
- owns local identity and peer recall tables
- periodically emits announces
- turns valid announces into `NodeInfoUpdateEvent` and `NodeProtocolUpdateEvent`
- sends opportunistic LXMF text packets
- receives and verifies opportunistic LXMF text packets
- emits proofs for received packets
- exposes incoming text to `ChatService`

### 6. UI / service integration

Phase 1 UI integration should be minimal and honest:

- add `LXMF` as a first-class protocol option
- keep `RNode Bridge` explicit for the host-controlled modem role
- allow Contacts and Chat pages to work when `LxmfAdapter` reports text support
- continue showing the existing RNode host-only warnings only for `RNode Bridge`

## Data model implications

### Protocol enum

Add:

- `MeshProtocol::LXMF`

Keep:

- `MeshProtocol::RNode`

for bridge mode until radio ownership and bridge/runtime selection are separated.

### Contact identity mapping

The current app-wide contact/chat model only has a 32-bit `NodeId`, while Reticulum/LXMF identities are addressed by 16-byte destination hashes.

Phase 1 therefore introduces a stable surrogate:

- `node_id = lower_32_bits(destination_hash)`

This is acceptable for a first pass, but it is not collision-proof. The long-term fix is to add a protocol-native peer identifier model to contacts/chat storage.

### Radio settings

Phase 1 reuses the existing `rnode_config` as the carrier configuration for `LXMF`. This avoids adding a second identical LoRa profile while the runtime still treats RNode as the Reticulum-compatible carrier.

## Phase-1 implementation details

### Identity

- generate Curve25519 keys with the already available Arduino `Curve25519` library
- generate Ed25519 keys with the existing shared Ed25519 implementation already used by MeshCore helpers
- persist under a dedicated preferences namespace

### Local destination

The local delivery destination is:

- app name: `lxmf`
- aspect: `delivery`
- direction: inbound single destination

### Announce behavior

The device should:

- send an announce shortly after startup/config apply
- re-announce periodically while active
- include `display_name` in LXMF announce app-data
- validate inbound announce signatures
- remember peer public keys and display names

### Text send

Phase 1 sends only opportunistic single-packet LXMF messages:

- empty title
- text content in the LXMF content field
- empty fields map
- no stamps or tickets
- fail fast if the packed message exceeds the single-packet budget

### Text receive

On inbound Reticulum data packets:

- decrypt if the destination hash matches local delivery destination
- reconstruct the full LXMF frame
- validate the LXMF signature if the source identity is known from prior announce
- queue a `MeshIncomingText`
- immediately emit a Reticulum proof packet for the received packet

## Deferred work after phase 1

### Phase 2 current slice

The current phase-2 implementation extends the phase-1 subset with:

- outbound `rnstransport.path.request` packets for known LXMF peers
- inbound path-request handling for the local `lxmf.delivery` destination
- `PATH_RESPONSE` announce replies for the local destination
- persisted peer recall so known LXMF peers survive reboot/config reload
- generic Reticulum announce validation and path learning, not only `lxmf.delivery`
- third-party announce cache storage and cache-request replay
- immediate announce rebroadcast as a minimal propagation mechanism
- `HEADER_2` transport packet parsing/building for multi-hop forwarding
- blind forwarding of transported non-local packets based on a local path table
- reverse-path tracking for proof relay
- opaque link-request relay plus link/resource packet relay over learned link IDs

This is still intentionally narrower than full Reticulum transport parity:

- no dedicated local `Link` API or destination-owned link termination on device
- no local resource sender/receiver state machine equivalent to Python `RNS.Resource`
- no propagation-node store/forward policy layer
- no shared-instance parity, tunnel handling, or management destinations
- no ratchets, ticket/stamp enforcement, or deep interop test coverage yet

### Phase 2

- path request / path response
- peer/path table persistence
- better peer ID model than 32-bit surrogate

### Phase 3

- Reticulum links
- direct link-based LXMF delivery
- resource transfer for messages larger than the opportunistic limit

### Phase 4

- propagation nodes
- ticket and stamp handling
- ratchets
- deeper parity testing against Python Reticulum/LXMF

## Code changes planned in this round

This round should land the following:

- shared Reticulum packet/token helpers
- shared LXMF wire/msgpack helpers
- ESP-side LXMF identity persistence
- ESP-side `LxmfAdapter` over the current RNode raw carrier
- protocol enum/UI/config updates for `LXMF`
- compile validation on `tlora_pager_sx1262`

## Non-goal for this round

This round does **not** claim "complete Reticulum parity".

It does claim something narrower and honest:

- Trail Mate gains a real device-side Reticulum/LXMF foundation
- the implementation uses protocol-accurate wire formats
- the shipped feature set is intentionally the opportunistic direct-neighbor subset first
