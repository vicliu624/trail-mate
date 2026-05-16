# `apps/linux_sim_shell`

Role = product app shell / target app shell.

`apps/linux_sim_shell` is the Linux simulator app shell baseline.

It is not a CMake build entrypoint directory. Phase 8 Correction adds a minimal
CMake target and smoke test so the app shell is executable without pulling in
the transitional simulator implementation root.

```text
Build Entrypoint invokes.
App Shell composes.
Target chooses.
Board describes.
UX Pack presents.
```

## Build Entrypoint

Build entrypoint = `builds/linux_cmake`

Historical source identity = `removed root linux_sim`

## Future Responsibilities

May:

- select Linux simulator target profile
- select simulator device profile
- select simulator UX profile
- invoke product composition
- hand off to runtime facade

Must not:

- own build host files
- own CMake project mechanics
- define board facts
- implement HAL details
- implement screen internals
- assemble Chat/Map/GPS runtime directly in build wrapper

## Thin App Shell Entrypoint

Current source:

- `src/linux_sim_app_shell.h`
- `src/linux_sim_app_shell.cpp`
- `tests/linux_sim_app_shell_smoke.cpp`

```text
target_id = linux_sim
ux_pack_id = simulator_full
historical_source = removed root linux_sim
```

No simulator runtime behavior changes in Phase 8 Correction.
