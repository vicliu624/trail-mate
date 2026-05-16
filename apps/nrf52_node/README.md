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

Historical source identity = `removed root esp_pio` and `removed root gat562_mesh_evb_pro`

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
historical_generic_root_name = removed root esp_pio
historical_board_root_name = removed root gat562_mesh_evb_pro
historical_role = pre-refactor PlatformIO/nRF52 implementation roots
replacement_owner = apps/nrf52_node + builds/pio_nrf52 + boards/gat562_mesh_evb_pro
```

No behavior change in Phase 8 Build/AppShell Executable Convergence.
