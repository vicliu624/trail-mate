# nRF52 Platform Owners

This directory owns nRF52 platform glue that is not a board fact and not an app
shell decision.

- `arduino_common/` contains shared nRF52 Arduino infrastructure.
- `debug/` owns the nRF52 serial debug console.
- `protocol/` owns nRF52 protocol adapter construction.
- `runtime/` owns nRF52 runtime apply services for active board/app state.

GAT562 board hardware facts remain in `boards/gat562_mesh_evb_pro/`.
The nRF52 node app lifecycle remains in `apps/nrf52_node/`.
