# App Shell Manifest: linux_sim_shell

## Role

Product app shell / target app shell.

## Target Family

Linux simulator.

## Renderer Family

Simulator, ASCII, or host UI selected by target profile.

## Build Entrypoint

Future authoritative build entrypoint:

- `builds/linux_cmake`

Current transitional path:

- `apps/linux_sim`

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

## Thin App Shell Entrypoint Declaration

Future declaration:

```text
trail_mate_linux_sim_shell_start(target_profile)
```

## Current Status

Skeleton only.

No behavior change in Phase 8.3.
