# Legacy Linux App Adapter Burn-Down Audit

This audit covers the first Linux-side dependency shrink after legacy root
burn-down started. It does not delete `legacy/app_implementations/linux_sim`
or `legacy/app_implementations/linux_uconsole`. It removes the final app shell
dependency on the legacy implementation adapters.

## `linux_sim_legacy_implementation_adapter`

- Current header:
  `legacy/app_implementations/linux_sim/src/linux_sim_legacy_implementation_adapter.h`
- Current source:
  `legacy/app_implementations/linux_sim/src/linux_sim_legacy_implementation_adapter.cpp`
- Current callers:
  `legacy/app_implementations/linux_sim/tests/linux_sim_legacy_implementation_adapter_smoke.cpp`
  and legacy-local CMake only.
- Current CMake include/source usage:
  `legacy/app_implementations/linux_sim/CMakeLists.txt` builds
  `src/linux_sim_legacy_implementation_adapter.cpp` into the
  `trailmate_linux_sim_legacy_local_adapter` compatibility target.
  `apps/linux_sim_shell/CMakeLists.txt` no longer compiles this source and no
  longer adds `legacy/app_implementations/linux_sim/src` as an include path.
- What it exposes to final app shell:
  historically it exposed only `implementation_root`, `app_shell`, and
  `target_id` strings plus a null-checking `validate()` method.
- Which part is still needed:
  final app shell still needs a transitional source descriptor to document the
  historical root, but it does not need a legacy-root-owned adapter.
- Replacement owner:
  `apps/linux_sim_shell/src/linux_sim_legacy_source_descriptor.h` and
  `apps/linux_sim_shell/src/linux_sim_legacy_source_descriptor.cpp`.
- Migration decision:
  final app shell now owns `LinuxSimLegacySourceDescriptor`; the old adapter is
  legacy-local compatibility only.

## `uconsole_legacy_implementation_adapter`

- Current header:
  `legacy/app_implementations/linux_uconsole/src/uconsole_legacy_implementation_adapter.h`
- Current source:
  `legacy/app_implementations/linux_uconsole/src/uconsole_legacy_implementation_adapter.cpp`
- Current callers:
  `legacy/app_implementations/linux_uconsole/tests/uconsole_legacy_implementation_adapter_smoke.cpp`
  and legacy-local CMake only.
- Current CMake include/source usage:
  `legacy/app_implementations/linux_uconsole/CMakeLists.txt` builds
  `src/uconsole_legacy_implementation_adapter.cpp` into the
  `trailmate_linux_uconsole_legacy_local_adapter` compatibility target.
  `apps/linux_uconsole_gtk/CMakeLists.txt` no longer compiles this source and
  no longer adds `legacy/app_implementations/linux_uconsole/src` as an include
  path.
- What it exposes to final app shell:
  historically it exposed only `implementation_root`, `app_shell`, and
  `target_id` strings plus a null-checking `validate()` method.
- Which part is still needed:
  final app shell still needs a transitional source descriptor to document the
  historical root, but it does not need a legacy-root-owned adapter.
- Replacement owner:
  `apps/linux_uconsole_gtk/src/linux_uconsole_gtk_legacy_source_descriptor.h`
  and
  `apps/linux_uconsole_gtk/src/linux_uconsole_gtk_legacy_source_descriptor.cpp`.
- Migration decision:
  final app shell now owns `LinuxUConsoleGtkLegacySourceDescriptor`; the old
  adapter is legacy-local compatibility only.

## Boundary Result

- Required audit tokens: replacement owner; migration decision;
  legacy-local compatibility only.
- `apps/linux_sim_shell` does not include
  `linux_sim_legacy_implementation_adapter`.
- `apps/linux_sim_shell/CMakeLists.txt` does not compile
  `linux_sim_legacy_implementation_adapter.cpp`.
- `apps/linux_uconsole_gtk` does not include
  `uconsole_legacy_implementation_adapter`.
- `apps/linux_uconsole_gtk/CMakeLists.txt` does not compile
  `uconsole_legacy_implementation_adapter.cpp`.
- Old adapter headers and sources remain only for legacy-local compatibility
  smoke coverage and historical root burn-down tracking.
