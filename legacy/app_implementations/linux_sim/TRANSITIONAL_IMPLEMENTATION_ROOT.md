# Transitional Implementation Root: linux_sim

This former transitional implementation root currently contains archived
historical Linux simulator implementation source.

It is not the final app shell semantic root.

Current path:

- `legacy/app_implementations/linux_sim`

Original historical path:

- `apps/linux_sim`

Final app shell:

- `apps/linux_sim_shell`

Authoritative build wrapper:

- `builds/linux_cmake`

Current status:

- archive-only historical root
- retains simulator implementation files under `archive/`
- does not own active CMake build targets
- retained only as reference material until targeted deletion

Exit condition:

- `apps/linux_sim_shell` owns the app shell startup contract;
- `builds/linux_cmake` invokes the app shell;
- `apps/linux_sim_shell` owns the transitional source descriptor;
- final app shell validation no longer depends on this local CMake root;
- archived simulator implementation can be deleted after final app shell covers
  all simulator workflows.
