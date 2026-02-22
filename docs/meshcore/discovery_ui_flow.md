# MeshCore Discovery UI Flow (Contacts / Filter Panel)

## Goal

Add an active discovery entry in Contacts page for MeshCore mode, so users can:

- discover neighbors proactively (`scan local`)
- expose local identity on demand (`send id local`)
- expose identity network-wide (`send id broadcast`)

Current issue: device can respond to discovery but cannot initiate it from UI.

## Entry Point

Location: Contacts page, `Filter Panel` (left column).

New button:

- label: `Discover`
- visible only when active protocol is `MeshCore`
- hidden in `Meshtastic` mode
- behavior: acts as a filter mode button (same interaction model as Contacts/Nearby/Broadcast/Team)

Rationale:

- user requested placement in Filter Panel
- unify with existing left-column mode buttons
- avoid dual-state confusion between `CHECKED`(mode) and `FOCUSED`(cursor)

## Interaction Flow

### 1) Enter Discover Mode

When user focuses/clicks `Discover`, second column (`List Container`) shows four action items:

1. `Scan Local`
2. `Send ID Local`
3. `Send ID Broadcast`
4. `Cancel`

Focus/encoder flow remains identical to other modes: left column selects mode, right column executes row action.

### 2) Scan Local

Definition:

- send one `DISCOVER_REQ` control packet in zero-hop direct mode
- wait for `DISCOVER_RESP` matching request tag
- collect responses for a short window

UI behavior:

1. keep Discover mode page visible
2. show non-blocking toast: `Scanning 5s...`
3. update nearby list as responses arrive (background data update)
4. after timeout, show summary toast with gained/total nearby count

Protocol mapping:

- route type: `DIRECT`, `path_len=0`
- payload: `DISCOVER_REQ`
- type filter default: all supported advertise types
- `prefix_only`: false by default
- `since`: 0

### 3) Send ID Local

Definition:

- publish one identity advert for local neighbors only

UI behavior:

1. stay on Discover mode page
2. send command
3. toast result:
   - success: `ID sent (local)`
   - failure: `ID send failed (local)`

Protocol mapping:

- route type: `DIRECT`, `path_len=0`
- payload type: `ADVERT`
- payload content: signed advert using local Ed25519 identity

### 4) Send ID Broadcast

Definition:

- publish identity advert intended for mesh-wide propagation

UI behavior:

1. stay on Discover mode page
2. send command
3. toast result:
   - success: `ID sent (broadcast)`
   - failure: `ID send failed (broadcast)`

Protocol mapping:

- route type: `FLOOD`, `path_len=0`
- payload type: `ADVERT`
- payload content: signed advert using local Ed25519 identity

## Focus Rules

- Discover is a normal mode button in the filter column.
- Enter from filter column moves focus to the list column.
- `Cancel` action exits Discover mode and returns to previous non-Discover mode in filter column.

## Data and List Behavior

On discovery response:

- create/update node entry with protocol = `MeshCore`
- update `Nearby` list immediately
- do not auto-promote to named contact

Broadcast list remains channel-centric and protocol-tagged (already separated by MT/MC labels).

## Backend Contract (for implementation)

To avoid UI -> concrete adapter coupling, add capability and command API in `IMeshAdapter`.

Suggested shape:

- `MeshCapabilities::supports_discovery_actions`
- `enum class DiscoveryAction { ScanLocal, SendIdLocal, SendIdBroadcast }`
- `bool triggerDiscoveryAction(DiscoveryAction action)` in `IMeshAdapter`
- passthrough in `ChatService`

MeshCore adapter implements all three actions.
Meshtastic adapter returns false.

## Logging (required)

Add explicit TX logs:

- `[MESHCORE] TX DISCOVER_REQ mode=local tag=... filter=... prefix=...`
- `[MESHCORE] TX ADVERT mode=local ...`
- `[MESHCORE] TX ADVERT mode=broadcast ...`

Reason: field debugging currently depends on serial logs.

## Acceptance Checklist

1. In MeshCore mode, Contacts Filter Panel shows `Discover`; Meshtastic mode hides it.
2. Selecting `Discover` switches to Discover mode and shows exactly 4 action rows in list column.
3. `Scan Local` sends discover request and summary count is shown after timeout.
4. `Send ID Local` and `Send ID Broadcast` both produce clear success/failure feedback.
5. New nodes from discover responses appear in Nearby list under MeshCore protocol.
6. No regression to existing Contacts/Nearby/Broadcast/Team flows.
