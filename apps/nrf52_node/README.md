# `apps/nrf52_node`

Role = product app shell / target app shell.

`apps/nrf52_node` is the future nRF52 node product app shell skeleton.

It is not a PlatformIO project directory and does not contain build host files
in Phase 8.3.

```text
Build Entrypoint invokes.
App Shell composes.
Target chooses.
Board describes.
UX Pack presents.
```

## Build Entrypoint

Build entrypoint = `builds/pio_nrf52`

Current transitional path = `apps/esp_pio` and `apps/gat562_mesh_evb_pro`

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

This is a declaration of intent only. No behavior change in Phase 8.3.
