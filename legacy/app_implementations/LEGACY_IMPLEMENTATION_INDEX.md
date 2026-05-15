# Legacy Implementation Index

This index lists historical implementation roots that used to be mixed with
final app shells. They are not product app shells. Each entry names the
current containment path, original historical path, final app shell,
authoritative build wrapper, and exit condition.

## ESP-IDF

- Historical path: `apps/esp_idf`
- Current path: `legacy/app_implementations/esp_idf`
- Final app shell: `apps/esp32_lvgl`
- Authoritative build wrapper: `builds/esp_idf`
- Exit condition: ESP-IDF component registration and target configuration are
  invoked only through the build wrapper and app shell, while reusable runtime
  behavior lives in official modules.

## ESP PIO

- Historical path: `apps/esp_pio`
- Current path: `legacy/app_implementations/esp_pio`
- Final app shell: `apps/nrf52_node`
- Authoritative build wrapper: `builds/pio_nrf52`
- Exit condition: PIO compatibility glue is no longer a product semantic root
  and each concrete device build is selected by the authoritative wrapper.

## Linux uConsole

- Historical path: `apps/linux_uconsole`
- Current path: `legacy/app_implementations/linux_uconsole`
- Final app shell: `apps/linux_uconsole_gtk`
- Authoritative build wrapper: `builds/linux_cmake`
- Exit condition: GTK/uConsole runtime code consumes the presentation graph
  from the composition root and remaining reusable code moves to modules or
  platform adapters.

## Linux Simulator

- Historical path: `apps/linux_sim`
- Current path: `legacy/app_implementations/linux_sim`
- Final app shell: `apps/linux_sim_shell`
- Authoritative build wrapper: `builds/linux_cmake`
- Exit condition: simulator UI/runtime code consumes the presentation graph
  through simulator-specific adapters and shared runtime behavior moves to
  official modules.

## Linux RPI

- Historical path: `apps/linux_rpi`
- Current path: `legacy/app_implementations/linux_rpi`
- Final app shell: future Linux device app shell
- Authoritative build wrapper: `builds/linux_cmake`
- Exit condition: Pi OS / Cardputer Zero device startup is represented by a
  final app shell and hardware-specific code remains behind platform/board
  boundaries.

## Linux UNO Q

- Historical path: `apps/linux_unoq`
- Current path: `legacy/app_implementations/linux_unoq`
- Final app shell: future Linux UNO Q app shell
- Authoritative build wrapper: `builds/linux_cmake`
- Exit condition: UNO Q product identity is represented by a final app shell
  or the placeholder is removed.

## GAT562 Mesh EVB Pro

- Historical path: `apps/gat562_mesh_evb_pro`
- Current path: `legacy/app_implementations/gat562_mesh_evb_pro`
- Final app shell: `apps/nrf52_node`
- Authoritative build wrapper: `builds/pio_nrf52`
- Exit condition: GAT562 remains the concrete device target while final nRF
  product semantics live in the app shell and build wrapper; board facts stay
  in `boards/gat562_mesh_evb_pro`.
