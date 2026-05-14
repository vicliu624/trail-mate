# Transitional Implementation Root: gat562_mesh_evb_pro

This directory currently contains board-specific PlatformIO implementation for
the GAT562 Mesh EVB Pro nRF52 target.

It is not the final app shell semantic root.

Final app shell:

- `apps/nrf52_node`

Authoritative build wrapper:

- `builds/pio_nrf52`

Current status:

- transitional implementation root
- board-specific transitional PlatformIO root
- retained to avoid changing current GAT562 build and flash behavior during
  Phase 8 Build/AppShell Executable Convergence

Exit condition:

- `builds/pio_nrf52` invokes `apps/nrf52_node`;
- `apps/nrf52_node` owns the nRF52 node app shell startup contract;
- `apps/gat562_mesh_evb_pro` remains only a board-specific legacy adapter root;
- board-specific PlatformIO configuration can move without behavior change.
