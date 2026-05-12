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

Does this PR let a radio adapter own direct message, PKI, peer key, contact, or
phone-core policy?

Does this PR let a GPS driver directly serve UI, BLE, Mesh, or storage policy?

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

