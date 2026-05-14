# Key Verification Ownership Audit

## Purpose

Phase 7.5 establishes ownership for key verification workflow state and
actions.

Key verification is a standalone runtime/presentation workflow. It is not
chat message delivery state, it is not `ChatWorkspaceModel` state, and it is
not renderer-local modal state.

## Current State

| Concern | Current owner | Current UI path | Problem |
| --- | --- | --- | --- |
| Verification modal lifecycle | `ChatUiController` | LVGL chat modal helpers | acceptable as legacy shell/widget ownership |
| Verification peer/nonce state | `ChatUiController` local fields | key verification modal | runtime workflow state is stored in renderer/controller |
| Verification code display | `ChatUiController` local formatting | final modal | code projection is mixed with widget construction |
| Verification number submission | `ChatUiController` | submit button | controller calls `IMeshAdapter::submitKeyVerificationNumber(...)` directly |
| Trust/accept action | `ChatUiController` | Trust Key button | controller calls `ContactService::setNodeKeyManuallyVerified(...)` directly |
| Peer label lookup | `ChatUiController` helper | modal text | controller reads contact service for projection data |
| Protocol differences | platform mesh adapters | EventBus events | MeshCore/Meshtastic details are not isolated behind a presentation adapter |

## Current Event Sources

Existing platform/runtime adapters publish these EventBus events:

- `KeyVerificationNumberRequestEvent`
- `KeyVerificationNumberInformEvent`
- `KeyVerificationFinalEvent`

The events carry peer id, nonce, number/code data, and final sender role. They
do not carry a portable presentation snapshot and they do not define UI
ownership.

## Current Actions

Number submission currently calls:

```text
IMeshAdapter::submitKeyVerificationNumber(peer_id, nonce, number)
```

Trust/accept currently calls:

```text
ContactService::setNodeKeyManuallyVerified(peer_id, true)
```

Both actions are legitimate runtime operations, but they must be invoked behind
an action sink rather than directly by `ChatUiController`.

## Boundary Rule

Key verification must not be owned by:

- `ChatWorkspaceModel`
- `MessageRow`
- renderer widgets
- `ChatUiController` local workflow state
- chat delivery read model

Key verification is owned by:

```text
KeyVerification runtime/session state
  -> IKeyVerificationPresentationSource
    -> KeyVerificationModel
      -> KeyVerificationSnapshot
```

Actions are owned by:

```text
KeyVerificationModel
  -> IKeyVerificationActionSink
    -> LegacyKeyVerificationActionSink / runtime adapter
```

## Target Ownership

Phase 7.5 introduces:

- `KeyVerificationSnapshot`
- `IKeyVerificationPresentationSource`
- `IKeyVerificationActionSink`
- `KeyVerificationModel`
- `LegacyKeyVerificationSession`
- `LegacyKeyVerificationSource`
- `LegacyKeyVerificationActionSink`

The legacy source/sink pair is an anti-corruption bridge around the existing
MeshCore/Meshtastic adapter surface and contact trust API.

## Phase 7.5 Decision

Phase 7.5 establishes the key verification runtime/presentation boundary first.

It does not:

- rewrite protocol key exchange
- rewrite key storage
- implement full trust management
- add key verification fields to `MessageRow`
- add key verification state to `ChatWorkspaceModel`
- make renderer modal state authoritative

## Remaining Legacy

The LVGL modal widgets may remain in `ChatUiController` during Phase 7.5.

That controller may open/close the modal, parse numeric user input from a
textarea, render a `KeyVerificationSnapshot`, and forward actions through
`KeyVerificationModel`.

It must not:

- compute verification codes
- store nonce/peer verification workflow state
- look up raw keys
- call mesh key verification APIs directly
- call contact trust APIs directly
