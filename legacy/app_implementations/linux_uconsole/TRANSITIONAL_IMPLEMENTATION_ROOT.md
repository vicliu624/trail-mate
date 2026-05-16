# Transitional Implementation Root: linux_uconsole

This former transitional implementation root currently contains archived
historical uConsole GTK implementation source.

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

- archive-only historical root
- retains GTK/uConsole implementation files under `archive/`
- does not own active CMake build targets
- retained only as reference material until targeted deletion

Exit condition:

- `apps/linux_uconsole_gtk` owns the app shell startup contract;
- `builds/linux_cmake` invokes the app shell;
- `apps/linux_uconsole_gtk` owns the transitional source descriptor;
- final app shell validation no longer depends on this local CMake root;
- archived GTK implementation can be deleted after descriptor-backed GTK
  renderer replaces the old widget/page path.
