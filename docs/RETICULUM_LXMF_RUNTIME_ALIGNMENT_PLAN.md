# Reticulum / LXMF Runtime Alignment Plan

## Purpose

This document defines the full Trail Mate adaptation target for device-side
Reticulum and LXMF support over the existing ESP RNode-compatible radio path.

It replaces the earlier phase-oriented view that treated `LxmfAdapter` as a
single adapter with incremental feature add-ons. The runtime has now grown into
 transport, path discovery, link relay, resource transfer, propagation, and
 business-level LXMF delivery concerns. Continuing to extend it as one large
 class would make protocol alignment, testing, and field debugging increasingly
 fragile.

The goal is to land a complete embedded runtime architecture that is:

- protocol-accurate enough to interoperate with upstream Reticulum/LXMF nodes
- structured enough to reason about path, link, and resource lifecycles
- observable enough to debug delivery failures in real deployments
- persistent enough to survive reboot without forgetting core identity and peer
  recall

## Scope

This plan covers the device-side runtime used when Trail Mate is operating in
 native `LXMF` mode. It does not redefine `RNode Bridge`, which remains the
 host-controlled modem path for an external Reticulum instance.

This plan is explicitly about:

- transport runtime behavior
- path discovery and path recovery
- link establishment and teardown
- resource transfer lifecycle
- LXMF direct and propagated delivery
- persistence and recovery rules
- test and validation criteria

This plan is not, by itself, a commitment to implement every upstream desktop
Reticulum feature such as management destinations or full shared-instance
service parity. The target is complete, system-level behavior for an embedded
device node.

## Current Baseline

The current `LxmfAdapter` already implements more than the original phase-1/2
documents describe:

- Reticulum announce validation and caching
- path request emission and path-response handling
- cached announce replay
- `HEADER_2` multi-hop forwarding
- reverse-path proof relay
- local link sessions
- link request relay
- resource advertise/request/hashmap/part/proof flows
- propagation offer/get request handling

The main problem is not missing packet codecs. The main problem is that these
capabilities are still governed by a monolithic adapter with limited runtime
lifecycle control.

## Current Implementation Status

The codebase now includes the first full runtime-alignment landing for this
plan.

Implemented runtime behaviors:

- shared runtime state models for transport, link, and propagation ownership
- destination-keyed pending path request tracking and timeout cleanup
- path-resolution clearing when matching announces are learned
- link close reasons and teardown-driven state cleanup
- timeout-driven path expiry and rediscovery trigger on failed outbound links
- outbound link establishment for locally initiated large-payload delivery
- deferred link payload queuing until the link becomes active
- active-link keepalive, stale transition, and timeout teardown behavior
- split-resource segment assembly for inbound multi-segment resource delivery

Still intentionally narrower than full upstream desktop/service parity:

- no shared-instance service runtime parity
- no management destinations or tunnel handling
- no metadata/compressed resource handling yet
- no deep automated interoperability matrix inside CI

## End-State Runtime Architecture

The end state is a layered runtime with the following modules.

### 1. Carrier Layer

`RNodeAdapter` remains responsible for:

- raw LoRa send/receive
- fragmentation/reassembly
- radio parameter application
- RX metadata capture

The carrier layer is unaware of Reticulum destinations, links, or LXMF payload
semantics.

### 2. Reticulum Transport Runtime

The transport runtime owns all network-level state that is independent of a
particular link payload.

Responsibilities:

- packet duplicate filtering
- path learning from announces
- path request lifecycle management
- pending local path request tracking
- announce cache, replay, hold, and release policy
- reverse-path proof routing
- `HEADER_1` and `HEADER_2` forwarding decisions
- local destination lookup
- link relay discovery state
- stale state culling and rediscovery triggers

Required state tables:

- `path_table`
- `destinations_map`
- `announce_table`
- `held_announces`
- `path_requests`
- `pending_local_path_requests`
- `reverse_table`
- `link_table`
- `packet_filter`

Implementation note:

On ESP targets these tables can remain bounded containers instead of unbounded
 maps, but they must still behave like keyed runtime tables with explicit
 lookup, lifetime, and eviction rules.

### 3. Reticulum Link Runtime

The link runtime owns encrypted session establishment and lifecycle.

Responsibilities:

- link request handling
- handshake proof validation
- RTT exchange
- keepalive and stale detection
- teardown and close reason classification
- pending request tracking
- validated-state tracking
- link-level packet proof handling
- resource runtime coordination

Required lifecycle:

- `Pending`
- `Handshake`
- `Active`
- `Stale`
- `Closed`

Every exit path from a live link must converge on a single teardown routine that
clears pending requests, cancels resources, shuts down channels, and erases
session secrets.

### 4. Reticulum Resource Runtime

The resource runtime owns large-payload delivery over links.

Responsibilities:

- advertisement encoding/decoding
- windowed part request scheduling
- hashmap segment tracking
- split-resource continuation
- cancel semantics
- proof completion semantics
- timeout and retry policy
- link-close cleanup

The target behavior is not the current "single-segment encrypted resource only"
subset. The runtime must be able to reason about:

- encrypted resources
- split resources
- resource proof timing
- in-flight cancellation
- partial hashmap knowledge

### 5. LXMF Service Layer

The LXMF service layer sits above Reticulum transport/link/resource machinery
and converts protocol behavior into product behavior.

Responsibilities:

- announce-derived contact updates
- opportunistic delivery
- direct link-based delivery
- propagation offer/get flows
- local chat/app-data injection
- proof/delivery result translation to UI/service events

This layer should not own route discovery or link teardown policy directly.

### 6. Persistence Layer

Persistence rules must be explicit.

Persisted state:

- local identity keypairs
- known peer identities and display names
- known destinations needed for recall
- durable propagation storage entries that survive reboot if product policy
  requires it

Ephemeral runtime state:

- packet filter entries
- reverse proof routes
- live link sessions
- in-flight resource transfers
- held announces
- pending path requests

## Functional Requirements

### Transport / Path

- Support direct and multi-hop announce learning.
- Track outstanding path requests per destination.
- Coalesce duplicate path requests for the same destination.
- Expire stale pending path requests.
- Release held announces when a pending request completes or times out.
- Rediscover paths after failed link establishment where policy allows.
- Route proofs using reverse-path state with explicit expiry.

### Link

- Support local link establishment for delivery and propagation destinations.
- Relay remote link traffic with explicit relay/link table state.
- Track link validation state separately from mere session presence.
- Tear down links deterministically on timeout, close packet, or local error.
- Cancel all live resources during teardown.
- Avoid leaving half-closed sessions until TTL cleanup.

### Resource

- Support outbound and inbound resource lifecycle management.
- Track segment and hashmap state explicitly.
- Support cancel semantics that also stop in-flight transfers.
- Associate resource state with link lifecycle.
- Reject unsupported resource forms only when policy explicitly says so.
- Prefer protocol-correct support over long-term permanent rejection.

### LXMF Delivery

- Opportunistic text/app-data delivery must remain supported.
- Link-based delivery should be the preferred path when a validated link exists.
- Propagation sync must use the same resource runtime rather than a side
  protocol path.
- Delivery success semantics must distinguish:
  - raw packet send
  - proof receipt
  - link response readiness
  - business-level response intent

## Implementation Strategy

The implementation proceeds in these layers, but each layer is expected to be
carried to a coherent, usable state before moving on.

### Stage 1: Runtime Decomposition

- Move path/transport state into a dedicated transport state container.
- Move link relay/session state into a dedicated link state container.
- Move propagation stores into a dedicated propagation state container.
- Move shared state types into a dedicated runtime state header.
- Keep the public `LxmfAdapter` API stable.

### Stage 2: Transport Lifecycle Completion

- Add destination-keyed pending path request tracking.
- Add explicit completion/timeout cleanup for path requests.
- Make path learning resolve pending requests.
- Add hooks for link-failure-driven rediscovery.
- Prepare local destination lookup as a first-class runtime concern.

### Stage 3: Link Lifecycle Completion

- Replace mark-only close behavior with real teardown.
- Introduce close reason tracking and stale-state transitions.
- Ensure timeout cleanup routes through the same teardown path.
- Make resource cancellation part of teardown.

### Stage 4: Resource Lifecycle Completion

- Normalize inbound and outbound transfer state machines.
- Add stronger cancellation semantics.
- Add split-resource support and proper segment continuation.
- Align proof timing and cancellation behavior with upstream expectations.

### Stage 5: Service and Interop Hardening

- Remove duplicated business logic across opportunistic/link/propagation paths.
- Add structured debug logging and counters.
- Add cross-validation against `.tmp/Reticulum` runtime behavior.

## Validation Matrix

The work is not complete until the following categories have been exercised.

- Direct announce discovery and opportunistic text delivery
- Multi-hop path request, path response, and proof return
- Link establish, keepalive, close, and timeout recovery
- Large link resource transfer with proof completion
- Resource cancellation while transfer is still active
- Propagation offer/get with mixed payload sizes
- Reboot recovery with persisted identity and peer recall intact

## Acceptance Criteria

The adaptation is considered complete when all of the following are true:

- The runtime is no longer governed by a single monolithic state blob.
- Transport, link, and resource lifecycle rules are explicit in code.
- Path requests and link teardown have dedicated runtime bookkeeping.
- Resource cleanup is tied to link lifecycle rather than passive TTL alone.
- The implementation can explain delivery success and failure at each stage.
- The architecture document and codebase describe the same system.

## Relationship To Existing Documents

- `RETICULUM_LXMF_DEVICE_MODE.md` remains useful as historical background and
  for earlier phase context.
- This document is the authoritative implementation target for the current
  alignment work.
