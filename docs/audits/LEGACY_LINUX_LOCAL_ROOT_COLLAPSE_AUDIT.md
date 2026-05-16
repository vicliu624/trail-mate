# Legacy Linux Local Root Collapse Audit

This audit records the collapse of the Linux legacy implementation roots into
archive-only directories. It does not affect ESP-IDF, PlatformIO, or GAT562
roots.

## `legacy/app_implementations/linux_sim`

- Current local CMake targets:
  none. `legacy/app_implementations/linux_sim/CMakeLists.txt` is an
  archive-only marker and must not define `add_library`, `add_executable`, or
  `add_test`.
- Current smoke targets:
  none under this root. Active validation lives in `apps/linux_sim_shell`,
  including `linux_sim_historical_source_descriptor_smoke`.
- Current composition root files:
  archived at
  `legacy/app_implementations/linux_sim/archive/composition/linux_sim_composition_root.h`
  and
  `legacy/app_implementations/linux_sim/archive/composition/linux_sim_composition_root.cpp`.
- Current old runtime/widget files:
  old SDL simulator and target entrypoint sources are archived under
  `legacy/app_implementations/linux_sim/archive/simulator/`.
- Which targets are still referenced by final app shells:
  none from this legacy root. `apps/linux_sim_shell` uses final-shell-owned
  descriptors and `modules/ui_ascii_runtime`.
- Which targets can be removed from normal build:
  all former local CMake targets, including the old simulator executable,
  composition root library, and legacy adapter smoke.
- Which files must remain as archive:
  historical simulator, composition, script, Docker, preset, and smoke
  sources retained under `archive/` until targeted deletion.
- Which files can move to tests/compatibility:
  the adapter-smoke meaning has already moved to
  `apps/linux_sim_shell/tests/linux_sim_historical_source_descriptor_smoke.cpp`.
  The old adapter smoke source and archived composition smoke source were
  deleted.
- Collapse decision:
  `linux_sim` local CMake root should become archive-only; final build
  validation lives in `apps/linux_sim_shell`.

## `legacy/app_implementations/linux_uconsole`

- Current local CMake targets:
  none. `legacy/app_implementations/linux_uconsole/CMakeLists.txt` is an
  archive-only marker and must not define `add_library`, `add_executable`, or
  `add_test`.
- Current smoke targets:
  none under this root. Active validation lives in `apps/linux_uconsole_gtk`,
  including `linux_uconsole_gtk_historical_source_descriptor_smoke`.
- Current composition root files:
  archived at
  `legacy/app_implementations/linux_uconsole/archive/composition/uconsole_composition_root.h`
  and
  `legacy/app_implementations/linux_uconsole/archive/composition/uconsole_composition_root.cpp`.
- Current old runtime/widget files:
  old GTK widget/page implementation, desktop fallback presenter, and target
  entrypoint sources are archived under
  `legacy/app_implementations/linux_uconsole/archive/gtk/`.
- Which targets are still referenced by final app shells:
  none from this legacy root. `apps/linux_uconsole_gtk` uses final-shell-owned
  descriptors and `modules/ui_gtk_runtime`.
- Which targets can be removed from normal build:
  all former local CMake targets, including the GTK app target, composition root
  library, old smoke targets, and legacy adapter smoke.
- Which files must remain as archive:
  historical GTK, desktop fallback, composition, packaging, tool, preset,
  and smoke sources retained under `archive/` until targeted deletion.
- Which files can move to tests/compatibility:
  the adapter-smoke meaning has already moved to
  `apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_historical_source_descriptor_smoke.cpp`.
  The old adapter smoke source and archived composition smoke source were
  deleted.
- Collapse decision:
  `linux_uconsole` local CMake root should become archive-only; final build
  validation lives in `apps/linux_uconsole_gtk`.

## Boundary Decision

- LinuxSim: local CMake root should become archive-only; final build validation
  lives in `apps/linux_sim_shell`.
- uConsole: local CMake root should become archive-only; final build validation
  lives in `apps/linux_uconsole_gtk`.
- Final app shells must not reference
  `legacy/app_implementations/linux_sim/archive` or
  `legacy/app_implementations/linux_uconsole/archive`.
