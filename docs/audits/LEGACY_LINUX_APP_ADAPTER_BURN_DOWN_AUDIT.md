# Legacy Linux App Adapter Burn-Down Audit

This audit covers the first Linux-side dependency shrink after legacy root
burn-down started. It does not delete `legacy/app_implementations/linux_sim`
or `legacy/app_implementations/linux_uconsole`. It removes the final app shell
dependency on the legacy implementation adapters.

## `linux_sim_legacy_implementation_adapter`

- Current header:
  deleted from `legacy/app_implementations/linux_sim/archive/adapters`.
- Current source:
  deleted from `legacy/app_implementations/linux_sim/archive/adapters`.
- Current callers:
  none in active source. The old adapter smoke was deleted after
  `apps/linux_sim_shell/tests/linux_sim_historical_source_descriptor_smoke.cpp`
  took over the compatibility assertion.
- Current CMake include/source usage:
  none. `legacy/app_implementations/linux_sim/CMakeLists.txt` is archive-only.
  `apps/linux_sim_shell/CMakeLists.txt` no longer compiles this source and no
  longer adds any LinuxSim legacy root include path.
- What it exposes to final app shell:
  historically it exposed only `implementation_root`, `app_shell`, and
  `target_id` strings plus a null-checking `validate()` method.
- Which part is still needed:
  final app shell still needs a historical source descriptor to document the
  historical root, but it does not need a legacy-root-owned adapter.
- Replacement owner:
  `apps/linux_sim_shell/src/linux_sim_historical_source_descriptor.h` and
  `apps/linux_sim_shell/src/linux_sim_historical_source_descriptor.cpp`.
- Migration decision:
  final app shell now owns `LinuxSimHistoricalSourceDescriptor`; the old adapter
  source has been deleted.

## `uconsole_legacy_implementation_adapter`

- Current header:
  deleted from `legacy/app_implementations/linux_uconsole/archive/adapters`.
- Current source:
  deleted from `legacy/app_implementations/linux_uconsole/archive/adapters`.
- Current callers:
  none in active source. The old adapter smoke was deleted after
  `apps/linux_uconsole_gtk/tests/linux_uconsole_gtk_historical_source_descriptor_smoke.cpp`
  took over the compatibility assertion.
- Current CMake include/source usage:
  none. `legacy/app_implementations/linux_uconsole/CMakeLists.txt` is
  archive-only. `apps/linux_uconsole_gtk/CMakeLists.txt` no longer compiles this
  source and no longer adds any uConsole legacy root include path.
- What it exposes to final app shell:
  historically it exposed only `implementation_root`, `app_shell`, and
  `target_id` strings plus a null-checking `validate()` method.
- Which part is still needed:
  final app shell still needs a historical source descriptor to document the
  historical root, but it does not need a legacy-root-owned adapter.
- Replacement owner:
  `apps/linux_uconsole_gtk/src/linux_uconsole_gtk_historical_source_descriptor.h`
  and
  `apps/linux_uconsole_gtk/src/linux_uconsole_gtk_historical_source_descriptor.cpp`.
- Migration decision:
  final app shell now owns `LinuxUConsoleGtkHistoricalSourceDescriptor`; the old
  adapter source has been deleted.

## Boundary Result

- Required audit tokens: replacement owner; migration decision;
  legacy-local compatibility only; archive-only.
- `apps/linux_sim_shell` does not include
  `linux_sim_legacy_implementation_adapter`.
- `apps/linux_sim_shell/CMakeLists.txt` does not compile
  `linux_sim_legacy_implementation_adapter.cpp`.
- `apps/linux_uconsole_gtk` does not include
  `uconsole_legacy_implementation_adapter`.
- `apps/linux_uconsole_gtk/CMakeLists.txt` does not compile
  `uconsole_legacy_implementation_adapter.cpp`.
- Old adapter headers and sources were deleted from archive-only historical
  source after descriptor smoke coverage moved to final app shells.
