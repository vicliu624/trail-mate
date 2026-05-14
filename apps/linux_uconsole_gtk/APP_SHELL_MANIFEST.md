# App Shell Manifest: linux_uconsole_gtk

## Role

Product app shell / target app shell.

## Target Family

Linux uConsole / AIO2-class handheld.

## Renderer Family

GTK.

## Build Entrypoint

Future authoritative build entrypoint:

- `builds/linux_cmake`

Current transitional path:

- `apps/linux_uconsole`

## Future Responsibilities

May:

- select Linux uConsole target profile
- select board/device package
- select GTK or desktop UX profile
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
trail_mate_linux_uconsole_gtk_start(target_profile)
```

## Current Status

Skeleton only.

No behavior change in Phase 8.3.
