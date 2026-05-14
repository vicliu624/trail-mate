# Transitional Implementation Root: linux_sim

This directory currently contains the executable Linux simulator implementation,
developer smoke tests, and CMake integration.

It is not the final app shell semantic root.

Final app shell:

- `apps/linux_sim_shell`

Authoritative build wrapper:

- `builds/linux_cmake`

Current status:

- transitional implementation root
- retains simulator implementation files
- retained to avoid changing runtime behavior during Phase 8 Correction

Exit condition:

- `apps/linux_sim_shell` owns the app shell startup contract;
- `builds/linux_cmake` invokes the app shell;
- `trailmate_linux_sim_legacy_adapter` exposes the transitional adapter
  descriptor;
- simulator implementation can be moved or wrapped without changing developer
  validation workflows.
