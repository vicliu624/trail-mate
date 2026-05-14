# Key Verification Runtime Specification

## Purpose

Define the runtime/presentation boundary for key verification workflows.

Key verification lets a user compare or submit protocol-specific verification
data for a peer and then accept, reject, refresh, or complete the workflow.

## Core Rule

Key verification is an independent runtime/presentation workflow.

It is not:

- chat delivery state
- `ChatWorkspaceModel` state
- `MessageRow` state
- renderer-local modal state

## Types

Phase 7.5 defines:

- `VerificationProtocol`
- `VerificationState`
- `VerificationFailureKind`
- `VerificationPromptKind`
- `KeyVerificationSnapshot`
- `KeyVerificationRequest`
- `IKeyVerificationPresentationSource`
- `IKeyVerificationActionSink`
- `KeyVerificationModel`
- `LegacyKeyVerificationSession`
- `LegacyKeyVerificationSource`
- `LegacyKeyVerificationActionSink`

## Snapshot

`KeyVerificationSnapshot` is a UI-readable projection. It may contain:

- peer id
- peer label
- protocol kind
- workflow state
- failure kind
- prompt kind
- verification code or number display
- status line
- command availability flags

It must not contain:

- private keys
- raw public keys
- PSK material
- protocol packets
- full peer records
- storage schema details

## Presentation Source

`IKeyVerificationPresentationSource` builds snapshots from runtime state.

It must not:

- render UI
- send messages
- mutate trust state
- submit verification numbers
- expose protocol packet details to renderers

## Action Sink

`IKeyVerificationActionSink` handles verification commands:

- accept
- reject
- refresh
- submit number

It must not build snapshots or render UI.

## Model

`KeyVerificationModel` is a portable presentation model. It stores only the
selected peer/protocol request and delegates all projection/action behavior to
its source and sink.

It must not:

- include LVGL or GTK
- include `ChatService`
- include `IMeshAdapter`
- read a key store
- compute verification codes
- own protocol-specific verification state

## Legacy Bridge

`LegacyKeyVerificationSource` and `LegacyKeyVerificationActionSink` are Phase
7.5 anti-corruption adapters.

They isolate current MeshCore/Meshtastic key verification helpers and contact
trust APIs behind the portable key verification model boundary.

The bridge may consume existing key verification events and update a bounded
legacy session. It must not expose protocol payload details to
`ChatUiController`.

## Chat UI Controller

`ChatUiController` may:

- receive legacy runtime events
- forward event data into the key verification legacy source
- select the peer in `KeyVerificationModel`
- render a modal from `KeyVerificationSnapshot`
- parse numeric input from the active textarea
- call `KeyVerificationModel` actions

It must not:

- store nonce or verification workflow state
- compute verification codes
- inspect raw key material
- call `IMeshAdapter::submitKeyVerificationNumber(...)`
- call `ContactService::setNodeKeyManuallyVerified(...)`

## Direct Message Intent

Direct-message failures such as peer-key-missing or verification-needed may be
translated into a key verification presentation intent in a later phase.

Phase 7.5 only establishes the target boundary so that future routing does not
pollute chat delivery, message rows, or renderer state.

## Non-Goals

Phase 7.5 does not implement full trust management.

Phase 7.5 does not rewrite MeshCore or Meshtastic key exchange.

Phase 7.5 does not make key verification durable.

Phase 7.5 does not add rich key verification rendering to chat messages.

Phase 7.5 does not add key verification to `ChatWorkspaceModel` or
`MessageRow`.
