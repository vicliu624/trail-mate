# Cross-Platform Product Architecture Review Checklist

Every PR that touches LoRa, GPS, BLE, UI, protocol, runtime, board support,
storage, app services, or target composition must answer these questions.

## Layer Identification

Which layer is changed?

```text
Target
Board
Platform
Runtime
Capability
Protocol
UseCase
AppService
PresentationModel
Renderer
CompositionRoot
```

If the answer is unclear, stop and update the specification or manifest before
continuing implementation.

## Boundary Questions

Does this PR introduce a new board-specific branch?

Does this PR let board code know business behavior?

Does this PR let a platform driver interpret protocol semantics?

Does this PR let a BLE host interpret Meshtastic or MeshCore phone protocol?

Does this PR add direct GPS, board, time, config, chat, contact, or mesh-adapter
reads to a BLE host instead of routing them through `modules/core_phone` and a
phone facade?

Does this PR put Meshtastic `ToRadio`/`FromRadio`, admin/config response,
queue/status encoding, or MeshCore command/contact/status/device-info semantics
back into `platform/*/ble`?

Does this PR let a radio adapter own direct message, PKI, peer key, contact, or
phone-core policy?

If this PR touches LoRa/Mesh, did it identify whether the change is Board Radio
Facts, Platform Radio Driver, Packet Radio Port, Mesh Protocol Core, Mesh
UseCase, Mesh Runtime Shell, or App Service/Chat integration?

Does this PR put direct-send, receive/decrypt/route, peer-key trust,
local-identity generation, ACK/retry/dedup, or PKI/identity flow into
`platform/*/chat/infra/*adapter*` instead of `modules/core_mesh`?

Does this PR make `platform/*/mesh` or `platform/*/radio` depend on AES,
Curve25519, Ed25519, protobuf business decisions, peer trust policy,
`DirectMessageService` internals, or protocol-specific direct-message flow?

If this PR adds or changes a store adapter, does the adapter only load, save,
remove, clear, and expose stored facts without deciding peer trust, key
exchange, direct-message eligibility, PKI fallback, or identity verification?

Does this PR keep `modules/core_mesh` free of Arduino, RadioLib, Preferences,
SQLite, FreeRTOS, Zephyr, ESP/nRF SDK, BLE, board, and UI headers?

Does this PR report direct-send failures as structured `SendResult`-style
causes (`PeerKeyMissing`, `LocalIdentityMissing`, `PacketBuildFailed`,
`RadioSendFailed`) instead of collapsing everything to `bool` in new core code?

Does this PR let a GPS driver directly serve UI, BLE, Mesh, or storage policy?

If this PR touches GPS or time, did it identify whether the change is Board
Facts, Platform GNSS Driver, GNSS Parser, LocationService, TimeAuthority, or a
consumer?

Does this PR make BLE, UI, Mesh, Team, or HostLink read `GpsService`, GPS UART,
board GPS, or NMEA parser directly instead of `ILocationSource`,
`ITimeAuthority`, or a device/status snapshot?

Does this PR let a platform GPS runtime shell own UI, chat, BLE phone protocol,
direct message, or contact-service behavior?

Does this PR let a UI renderer own business logic?

Does this PR let an app service know LVGL, GTK, ASCII, CLI, board pins, or
platform SDK objects?

Does this PR let shared core include platform, board, SDK, or UI headers?

Does this PR add cross-thread direct calls instead of event queues, command
queues, snapshots, or declared store locks?

Does this PR violate UI-thread-only rules?

Does this PR add blocking storage writes on a UI owner thread?

## Manifest Questions

Does this PR change product composition?

Does this PR change a capability state, endpoint host, binding, or proxy mode?

Does this PR change authority ownership?

Does this PR change runtime/task/thread ownership?

Does this PR change UI shell, renderer, layout profile, or input model?

Does this PR require updating a target manifest?

Does this PR require updating a board manifest?

## Required Statement

Each PR description should include:

```text
Architecture layer changed:
Target manifest updated: yes/no/not applicable
Board manifest updated: yes/no/not applicable
Runtime/concurrency impact:
Known boundary exceptions:
```

## Phase 4.5 Exit Gate

Before starting Phase 5 PresentationModel/UI shell work, run:

```text
python tools/architecture/check_phase45_ready.py
```

This gate is narrower than the non-blocking global boundary report. It must
pass before Phase 5 because it checks that:

- target manifests name `core_phone` and `core_mesh`;
- `core_phone` no longer exposes `ChatService`, `ContactService`,
  `IMeshAdapter`, or `INodeStore` as phone-core dependencies;
- `core_mesh` has no platform SDK, board, BLE, or UI includes;
- Phase 4.5 core mesh/phone/GPS test files exist;
- at least one actual ESP Meshtastic send path enters `MeshSession` and
  `DirectMessageService`;
- at least one actual ESP Meshtastic receive path enters
  `ReceivePacketService`;
- remaining Mesh/GPS/BLE exceptions are explicit burn-down items in
  `tools/architecture/KNOWN_PRODUCT_ARCHITECTURE_VIOLATIONS.md`.
