# Transitional Implementation Root: linux_uconsole

This directory currently contains the executable uConsole implementation and
CMake integration.

It is not the final app shell semantic root.

Final app shell semantic root:

- `apps/linux_uconsole_gtk`

Authoritative Linux build wrapper:

- `builds/linux_cmake`

Current status:

- transitional implementation root
- retains GTK/uConsole implementation files
- retained to avoid changing runtime behavior during Phase 8 Correction

Exit condition:

- `apps/linux_uconsole_gtk` owns the app shell startup contract;
- `builds/linux_cmake` invokes the app shell;
- GTK implementation can be moved or wrapped without changing target behavior.
