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

Historical source identities:

- `removed root esp_pio`
- `removed root gat562_mesh_evb_pro`

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

## Current Status

Executable app shell baseline.

Default UX Pack:

- `tiny_node_status`

UX Pack Runtime Binding:

- `Nrf52NodeAppShell::validate()` resolves `default_ux_pack_id` through
  product-composition target UX binding. The nRF52 shell stays independent of
  LVGL UX pack runtime code.

Historical source descriptor:

- `nrf52_historical_source_descriptor`

No behavior change in Phase 8 Build/AppShell Executable Convergence.
