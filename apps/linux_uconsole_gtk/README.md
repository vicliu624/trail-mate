# `apps/linux_uconsole_gtk`

Role = product app shell / target app shell.

`apps/linux_uconsole_gtk` is the future Linux uConsole/AIO2 GTK product app
shell baseline.

It is not a CMake build entrypoint directory. Phase 8 Correction adds a minimal
CMake target and smoke test so the app shell is executable without pulling in
the transitional GTK implementation root.

```text
Build Entrypoint invokes.
App Shell composes.
Target chooses.
Board describes.
UX Pack presents.
```

## Build Entrypoint

Build entrypoint = `builds/linux_cmake`

Historical source identity = `removed root linux_uconsole`

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

## Thin App Shell Entrypoint

Current source:

- `src/linux_uconsole_gtk_app_shell.h`
- `src/linux_uconsole_gtk_app_shell.cpp`
- `tests/linux_uconsole_gtk_app_shell_smoke.cpp`

```text
target_id = uconsole
ux_pack_id = uconsole_desktop
historical_source = removed root linux_uconsole
```

No GTK runtime behavior changes in Phase 8 Correction.
