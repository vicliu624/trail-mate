# Linux CMake Build Entrypoint

Authoritative Linux build entrypoint.

Build authority:

```text
Linux -> CMake
```

Current executable app shell wrappers:

```text
apps/linux_uconsole_gtk
apps/linux_sim_shell
```

Current CI owners:

```text
.github/workflows/uconsole-linux.yml
.github/workflows/linux-simulator.yml
```

There is intentionally no active `cardputer-zero-linux.yml` workflow. Cardputer
Zero Linux remains lower-completion device bring-up work until it has a
dedicated final app shell and hardware-oriented validation gate.

Current transitional implementation roots:

```text
removed root linux_sim
removed root linux_uconsole
removed root linux_rpi
removed root linux_unoq
```

Final wrapper direction:

```text
builds/linux_cmake
  -> invokes selected Linux app shell
```

The selected shell may be simulator, uConsole, Raspberry Pi, UNO Q, or a future
headless Linux target. The wrapper must not absorb product behavior from those
app shells.

Phase 8 Correction adds a real `CMakeLists.txt` here. It only invokes app shell
directories with `add_subdirectory`; it does not include
`TrailMateLinuxSources.cmake` or own Linux runtime composition.

Rules:

- thin wrapper only
- Build Entrypoint invokes
- App Shell composes
- do not assemble Chat/Map/GPS runtime here
- do not choose UX pack here
- do not define board facts here
- keep existing Linux CMake app paths stable until wrapper parity is proven
