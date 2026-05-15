# Transitional Implementation Root: linux_uconsole

This directory currently contains the executable uConsole implementation and
CMake integration.

It is not the final app shell semantic root.

Current path:

- `legacy/app_implementations/linux_uconsole`

Original historical path:

- `apps/linux_uconsole`

Final app shell:

- `apps/linux_uconsole_gtk`

Authoritative build wrapper:

- `builds/linux_cmake`

Current status:

- transitional implementation root
- retains GTK/uConsole implementation files
- retained to avoid changing runtime behavior during Phase 8 directory
  semantic convergence

Exit condition:

- `apps/linux_uconsole_gtk` owns the app shell startup contract;
- `builds/linux_cmake` invokes the app shell;
- `apps/linux_uconsole_gtk` owns the transitional source descriptor;
- the legacy adapter remains legacy-local compatibility only;
- GTK implementation can be moved or wrapped without changing target behavior.
