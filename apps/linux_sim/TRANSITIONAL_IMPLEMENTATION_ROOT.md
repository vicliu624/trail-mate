# Transitional Implementation Root: linux_sim

This directory currently contains the executable Linux simulator implementation,
developer smoke tests, and CMake integration.

It is not the final app shell semantic root.

Final app shell semantic root:

- `apps/linux_sim_shell`

Authoritative Linux build wrapper:

- `builds/linux_cmake`

Current status:

- transitional implementation root
- retains simulator implementation files
- retained to avoid changing runtime behavior during Phase 8 Correction

Exit condition:

- `apps/linux_sim_shell` owns the app shell startup contract;
- `builds/linux_cmake` invokes the app shell;
- simulator implementation can be moved or wrapped without changing developer
  validation workflows.
