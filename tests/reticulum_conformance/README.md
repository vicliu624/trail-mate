# Reticulum Conformance Tests

This directory holds dev-only Reticulum conformance fixtures and test plans.

It is intentionally separate from production runtime code. Files here may refer
to Python Reticulum or `attermann/microReticulum` as references, but those
references must not become product dependencies unless a later architecture
decision explicitly changes that boundary.

## Scope

The first conformance target is Trail Mate's Reticulum network-stack subset:

- `modules/core_chat/include/chat/infra/reticulum/reticulum_wire.h`
- `modules/core_chat/src/infra/reticulum/reticulum_wire.cpp`
- `modules/core_chat/include/chat/infra/lxmf/lxmf_wire.h`
- `modules/core_chat/src/infra/lxmf/lxmf_wire.cpp`
- `platform/esp/arduino_common/.../lxmf_adapter.*`
- `platform/esp/arduino_common/.../lxmf_runtime_state.h`
- `platform/esp/arduino_common/.../lxmf_transport_runtime.*`
- `platform/esp/arduino_common/.../lxmf_destination_registry.*`
- `platform/esp/arduino_common/.../lxmf_path_manager.*`
- `platform/esp/arduino_common/.../lxmf_link_runtime.*`
- `platform/esp/arduino_common/.../lxmf_link_manager.*`
- `platform/esp/arduino_common/.../lxmf_packet_router.*`
- `platform/esp/arduino_common/.../lxmf_ping_service.*`
- `platform/esp/arduino_common/.../lxmf_network_page_client.*`
- `platform/esp/arduino_common/.../lxmf_propagation_client.*`
- `platform/esp/arduino_common/.../lxmf_lxst_telephony_client.*`
- `platform/esp/arduino_common/.../lxmf_resource_runtime.*`
- `platform/esp/arduino_common/.../lxmf_propagation_runtime.*`
- `platform/esp/arduino_common/.../lxmf_propagation_service_runtime.*`
- `platform/esp/arduino_common/.../lxmf_delivery_runtime.*`

The tests should answer whether Trail Mate's packets and runtime decisions match
Reticulum behavior for the subset we claim to support.

## Directory Layout

| Path | Purpose |
| --- | --- |
| `fixtures/` | Reference-generated raw packets, parsed fields, and metadata |
| `deviations.md` | Open and resolved protocol deviations |
| `check_scope_guard.py` | Dev-only boundary check for the first conformance slice |
| `test_reticulum_supported_subset_vectors.cpp` | Trail Mate supported-subset golden vector baseline |
| `README.md` | Test scope and guardrails |

## Reference Policy

Use references in this order:

1. Python Reticulum for canonical behavior.
2. `microReticulum` for MCU/C++ implementation behavior.
3. Existing Trail Mate tests only for product compatibility checks.

When the references disagree, record the mismatch in `deviations.md` before
changing Trail Mate behavior.

## Fixture Metadata

Each fixture set must record:

- reference implementation name
- upstream repository URL
- exact commit hash
- generation date
- generation command
- fixed input values
- expected parsed fields
- whether the fixture is canonical, secondary, or exploratory

## Initial Test Families

### Trail Mate Supported-Subset Golden Baseline

The supported-subset baseline locks the byte shapes that Trail Mate currently
uses to cover existing product business paths. It is intentionally narrower
than full Reticulum/LXMF conformance and broader than the first announce-only
reference check.

The current baseline covers:

- full and truncated hash derivation
- LXMF delivery and propagation name hashes
- identity, destination, plain destination, and projected node-id derivation
- header-1 and header-2 packet construction/parsing
- packet hash, truncated packet hash, and proof packet bytes
- LXMF text payload, Trail Mate app-data payload, message hash, signed part,
  and packed envelope bytes
- link request and response payloads used by propagation service planning
- resource advertisement and hashmap-update payloads
- propagation offer, get, id-list, message-list, and batch payloads
- negative rejection for short Reticulum packets and truncated msgpack payloads

Its fixture metadata is exploratory until the same fixed inputs are
cross-checked against Python Reticulum and an interoperable LXMF reference.

### Golden Vectors

Byte-level checks for deterministic operations:

- hash derivation
- destination derivation
- header encoding
- packet hash derivation
- announce payload layout
- proof payload layout

### Parser Differentials

Reference raw packets parsed by Trail Mate:

- data packet
- announce packet
- link request packet
- proof packet
- header-2 transport packet

### Behavior Traces

Runtime-level traces where byte equality is not the right comparison:

- path learning
- path request and cached announce replay
- duplicate packet filtering
- reverse proof routing
- link establishment and timeout
- resource transfer completion

### Runtime Ownership Contract

Runtime ownership tests compile and exercise the state models that currently
hold Reticulum transport, link, resource, propagation, and verified-delivery
ownership for the embedded adapter. They do not claim byte-level
interoperability. They prevent the runtime from silently collapsing back into
unnamed adapter-local state while deeper behavior traces are being added. The
current smoke also exercises the
transport runtime table helpers for paths, duplicate filtering, reverse proof
routes, pending path requests, link relay lookup, and TTL cleanup, plus the link
runtime helpers for session lookup, close cleanup, stale/timeout decisions, and
expired-session removal.

## Out Of Scope

These concerns belong to product or LXMF-specific validation, not this
Reticulum conformance directory:

- Trail Mate team business semantics
- TeamKey ownership and verification
- UI protocol labels
- PC Link removal
- MQTT expansion behavior
- full LXMF propagation-service interoperability beyond the product-supported
  wire subset

## Guardrail

If a test needs to introduce a new product concept, it belongs in a design
document first. Conformance tests can reveal drift, but they must not create new
runtime semantics by accident.

Run the first boundary guard with:

```sh
python tests/reticulum_conformance/check_scope_guard.py
```

The guard also validates fixture metadata and basic raw-hex shape. It is not a
Reticulum parser; byte-level semantic checks belong in later conformance tests.

Run the first Trail Mate wire-parser smoke with:

```sh
cmake --build build/linux-simulator-debug --target trailmate_reticulum_announce_vectors_smoke
build/linux-simulator-debug/apps/linux_sim_shell/trailmate_reticulum_announce_vectors_smoke
```

That smoke parses the `microReticulum` announce vectors with Trail Mate's
`reticulum::parsePacket` and `reticulum::parseAnnounce` helpers. It verifies
header type, hops, rebroadcast header shape, ratchet layout, payload length,
app-data length, announce signature validity, and destination-hash validity.

Run the first runtime ownership contract smoke with:

```sh
cmake --build build/linux-simulator-debug --target trailmate_reticulum_runtime_state_contract_smoke
build/linux-simulator-debug/apps/linux_sim_shell/trailmate_reticulum_runtime_state_contract_smoke
```

That smoke verifies that the current embedded Reticulum runtime has explicit
transport, link, resource, and propagation state objects with the lifecycle
fields needed by the product business paths, and that the first extracted
transport table helpers preserve the adapter's expected lookup, upsert, resolve,
and cleanup behavior. It also verifies that the extracted link runtime preserves
session lookup, close cleanup, lifecycle transition, culling, and removal rules
without pulling product side effects into the runtime helper. The current
owner-manager slice verifies that destination registry, path manager, link
manager, packet router, ping service, pending network-page client,
propagation-client shell, and LXST telephony scratch owner remain explicit,
non-copyable owners rather than collapsing back into adapter-local vectors or
scratch fields. `AnnounceIngestor` also has a signed host regression in
`tests/reticulum_gateway/test_announce_restore.cpp`. It compiles the production
ingestor, verifier, crypto verification port, projection, registry and path
manager; only Serial and carrier declarations substitute for device headers.
It verifies requested cached-response peer recovery without refreshing route
age or rolling back current metadata, and rejects unsolicited responses,
expired requests/routes, historical announcements and invalid signatures.
ESP builds and the scope guard continue to cover the full adapter. The current
resource slice verifies incoming/outgoing transfer initialisation, hashmap
window requests, hashmap updates, part receipt bookkeeping, split-resource
assembly, proof completion, resource lookup/cancel helpers, and resource TTL
culling as runtime-owned behavior. The propagation slice verifies peer
upsert/eviction, seen/incoming/served counters, transient duplicate tracking,
held-entry lookup/removal, offer wanted-id selection, get-message selection with
transfer limits, and propagation TTL culling without moving LXMF wire
encode/decode or local queue side effects into the runtime helper. The delivery
slice verifies that verified LXMF packed payload bytes are classified as
Trail Mate app-data or LXMF text, then materialised into Trail Mate
`MeshIncoming*` objects with Reticulum peer identity and RX metadata preserved,
including one queue push/pop check for text identity retention. It still leaves
outer message envelope validation and queue pressure policy outside the delivery
runtime.

The propagation service slice verifies that `/offer` and `/get` link requests
are planned outside the adapter: offer validation, wanted-id decisions, held-id
listing, message selection, and served counters now produce a packed link
response plan while the adapter remains responsible for sending that response.

Run the Trail Mate supported-subset golden vector baseline with:

```sh
cmake --build build/linux-simulator-debug --target trailmate_reticulum_supported_subset_vectors_smoke
build/linux-simulator-debug/apps/linux_sim_shell/trailmate_reticulum_supported_subset_vectors_smoke
```

That smoke verifies the current product-supported Reticulum/LXMF byte subset
against `fixtures/trailmate_supported_subset_vectors.json` and the constants in
the C++ runner. It uses a dev-only `TRAIL_MATE_RETICULUM_HASH_ONLY` build mode
so local conformance can still verify SHA-256-dependent vectors on machines
where CMake cannot find OpenSSL headers. AES/token paths remain outside that
target.
