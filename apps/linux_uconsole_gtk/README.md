# `apps/linux_uconsole_gtk`

Role = product app shell / target app shell.

`apps/linux_uconsole_gtk` is the future Linux uConsole/AIO2 GTK product app
shell skeleton.

It is not a CMake build entrypoint directory and does not contain build host
files in Phase 8.3.

```text
Build Entrypoint invokes.
App Shell composes.
Target chooses.
Board describes.
UX Pack presents.
```

## Build Entrypoint

Build entrypoint = `builds/linux_cmake`

Current transitional path = `apps/linux_uconsole`

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

This is a declaration of intent only. No behavior change in Phase 8.3.
