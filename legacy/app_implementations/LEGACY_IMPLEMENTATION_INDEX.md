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
- Burn-down status: contained historical ESP-IDF component root.
- Deletion blocker: ESP-IDF component registration and target sdkconfig defaults
  still physically live under this root.
- Replacement owner: `apps/esp32_lvgl` plus `builds/esp_idf`.
- Next deletion task: transfer component registration ownership into the
  authoritative build wrapper without changing ESP-IDF target semantics.

## ESP PIO

- Historical path: `apps/esp_pio`
- Current path: `legacy/app_implementations/esp_pio`
- Final app shell: `apps/nrf52_node`
- Authoritative build wrapper: `builds/pio_nrf52`
- Exit condition: PIO compatibility glue is no longer a product semantic root
  and each concrete device build is selected by the authoritative wrapper.
- Burn-down status: contained PlatformIO compatibility root.
- Deletion blocker: `builds/pio_nrf52/platformio.ini` still references this root
  through source/include layout.
- Replacement owner: `apps/nrf52_node` plus `builds/pio_nrf52`.
- Next deletion task: split PIO compatibility source ownership from concrete
  GAT562 validation and make this root forwarding-only.

## Linux uConsole

- Historical path: `apps/linux_uconsole`
- Current path: `legacy/app_implementations/linux_uconsole`
- Final app shell: `apps/linux_uconsole_gtk`
- Authoritative build wrapper: `builds/linux_cmake`
- Exit condition: GTK/uConsole runtime code consumes the presentation graph
  from the composition root and remaining reusable code moves to modules or
  platform adapters.
- Burn-down status: contained historical GTK implementation root.
- Deletion blocker: old GTK widget/page implementation, local CMake target, and
  adapter smoke coverage still live under this root.
- Replacement owner: `apps/linux_uconsole_gtk` plus `modules/ui_gtk_runtime`.
- Next deletion task: make real GTK widget creation consume descriptor page
  registry output before retiring the local CMake root.

## Linux Simulator

- Historical path: `apps/linux_sim`
- Current path: `legacy/app_implementations/linux_sim`
- Final app shell: `apps/linux_sim_shell`
- Authoritative build wrapper: `builds/linux_cmake`
- Exit condition: simulator UI/runtime code consumes the presentation graph
  through simulator-specific adapters and shared runtime behavior moves to
  official modules.
- Burn-down status: contained historical simulator root.
- Deletion blocker: legacy simulator composition root, local CMake target,
  adapter smoke coverage, and old simulator scripts still live under this root.
- Replacement owner: `apps/linux_sim_shell` plus `modules/ui_ascii_runtime`.
- Next deletion task: move or retire simulator composition/script ownership once
  final app-shell smoke coverage no longer needs the legacy adapter.

## Linux RPI

- Historical path: `apps/linux_rpi`
- Current path: `legacy/app_implementations/linux_rpi`
- Final app shell: future Linux device app shell
- Authoritative build wrapper: `builds/linux_cmake`
- Exit condition: Pi OS / Cardputer Zero device startup is represented by a
  final app shell and hardware-specific code remains behind platform/board
  boundaries.
- Burn-down status: retained historical Linux device bring-up root.
- Deletion blocker: no final Linux RPI/Cardputer Zero app shell owns this product
  path yet.
- Replacement owner: future Linux device app shell plus `platform/linux`.
- Next deletion task: create or select the final Linux device app shell before
  moving build ownership.

## Linux UNO Q

- Historical path: `apps/linux_unoq`
- Current path: `legacy/app_implementations/linux_unoq`
- Final app shell: future Linux UNO Q app shell
- Authoritative build wrapper: `builds/linux_cmake`
- Exit condition: UNO Q product identity is represented by a final app shell
  or the placeholder is removed.
- Burn-down status: placeholder historical root.
- Deletion blocker: target ownership decision is still open.
- Replacement owner: future Linux UNO Q app shell or explicit retirement.
- Next deletion task: decide whether UNO Q is a real product target, then create
  the final app shell or delete the placeholder.

## GAT562 Mesh EVB Pro

- Historical path: `apps/gat562_mesh_evb_pro`
- Current path: `legacy/app_implementations/gat562_mesh_evb_pro`
- Final app shell: `apps/nrf52_node`
- Authoritative build wrapper: `builds/pio_nrf52`
- Exit condition: GAT562 remains the concrete device target while final nRF
  product semantics live in the app shell and build wrapper; board facts stay
  in `boards/gat562_mesh_evb_pro`.
- Burn-down status: contained concrete board historical root.
- Deletion blocker: concrete GAT562 compatibility marker is still referenced by
  the nRF app shell and PIO wrapper.
- Replacement owner: `apps/nrf52_node` plus `boards/gat562_mesh_evb_pro`.
- Next deletion task: prove concrete GAT562 validation through the final nRF app
  shell and build wrapper before retiring this app-semantic root.
