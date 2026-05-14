# `apps/nrf52_node`

Role = product app shell / target app shell.

`apps/nrf52_node` is the nRF52 node product app shell executable baseline.

It is not a PlatformIO project directory and does not contain build host files
in Phase 8 Build/AppShell Executable Convergence.

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

Current source-level shell descriptor:

```text
src/nrf52_node_app_shell.h
src/nrf52_node_app_shell.cpp
trailmate-nrf52-node-app-shell
```

Current config:

```text
target_family = nrf52_node
default_ux_pack_id = tiny_node_status
transitional_source = apps/esp_pio
board_specific_transitional_source = apps/gat562_mesh_evb_pro
legacy_adapter_target = trailmate_nrf52_pio_legacy_adapter
```

No behavior change in Phase 8 Build/AppShell Executable Convergence.
