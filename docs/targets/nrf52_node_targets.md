# nRF52 Node Target Profiles

Phase 8.3 target profile baseline only.

No behavior change in Phase 8.3.

```text
Build Entrypoint invokes.
App Shell composes.
Target chooses.
Board describes.
UX Pack presents.
```

| Target | Board | Platform | Build entrypoint | App shell | UX profile | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `gat562` | `gat562_mesh_evb_pro` | nRF52 | `builds/pio_nrf52` | `apps/nrf52_node` | `tiny_node_status` | transitional |
| `nrf52_node_dev` | target-selected nRF52 board | nRF52 | `builds/pio_nrf52` | `apps/nrf52_node` | `tiny_node_status` | planned |

## Transitional Source

Current transitional paths:

- `legacy/app_implementations/esp_pio`
- `legacy/app_implementations/gat562_mesh_evb_pro`
- root `platformio.ini`

The final `apps/nrf52_node` shell should select target profile and hand off to
product composition. It should not absorb PlatformIO environment definitions or
board facts.

## Non-Goals

This document does not move PlatformIO files, root build configuration,
GAT562 source files, board packages, or mono UI code.
