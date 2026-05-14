# Transitional Implementation Root: esp_pio

This directory currently contains PlatformIO implementation and historical ESP
PIO build integration.

It is not the final app shell semantic root.

Final app shell:

- `apps/nrf52_node`

Authoritative build wrapper:

- `builds/pio_nrf52`

Current status:

- transitional implementation root
- transitional build root for existing PlatformIO compatibility
- retained to avoid changing current PlatformIO environments during Phase 8
  Build/AppShell Executable Convergence

Exit condition:

- `builds/pio_nrf52` invokes `apps/nrf52_node`;
- `apps/nrf52_node` owns the nRF52 node app shell startup contract;
- `apps/esp_pio` remains only a legacy adapter / compatibility build root;
- target-specific PlatformIO configuration can move without behavior change.
