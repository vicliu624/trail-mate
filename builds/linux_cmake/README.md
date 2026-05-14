# Linux CMake Build Entrypoint

Authoritative Linux build entrypoint.

Build authority:

```text
Linux -> CMake
```

Current transitional app shells:

```text
apps/linux_sim
apps/linux_uconsole
apps/linux_rpi
apps/linux_unoq
```

Final wrapper direction:

```text
builds/linux_cmake
  -> invokes selected Linux app shell
```

The selected shell may be simulator, uConsole, Raspberry Pi, UNO Q, or a future
headless Linux target. The wrapper must not absorb product behavior from those
app shells.

This directory is a Phase 8.2 migration stub. It does not yet contain top-level
CMake project files, presets, source-list rewrites, or generated build state.

Rules:

- thin wrapper only
- Build Entrypoint invokes
- App Shell composes
- do not assemble Chat/Map/GPS runtime here
- do not choose UX pack here
- do not define board facts here
- keep existing Linux CMake app paths stable until wrapper parity is proven
