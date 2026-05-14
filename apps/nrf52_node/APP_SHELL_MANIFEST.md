# App Shell Manifest: nrf52_node

## Role

Product app shell / target app shell.

## Target Family

nRF52.

## Renderer Family

Node/status UI, mono UI, or target-selected compact embedded renderer.

## Build Entrypoint

Future authoritative build entrypoint:

- `builds/pio_nrf52`

Current transitional path:

- `apps/esp_pio`
- `apps/gat562_mesh_evb_pro`

## Future Responsibilities

May:

- select nRF52 target profile
- select board package
- select node/status UX profile
- invoke product composition
- hand off to runtime facade

Must not:

- own build host files
- own PlatformIO environment definitions
- define board facts
- implement HAL details
- implement screen internals
- assemble Chat/Map/GPS runtime directly in build wrapper

## Thin App Shell Entrypoint Declaration

Future declaration:

```text
trail_mate_nrf52_node_start(target_profile)
```

## Current Status

Skeleton only.

No behavior change in Phase 8.3.
