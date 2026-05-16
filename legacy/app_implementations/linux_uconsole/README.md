# Legacy uConsole GTK Archive

This directory is an archived historical implementation root.

The active uConsole GTK app shell and validation path now live in:

- `apps/linux_uconsole_gtk`
- `modules/ui_gtk_runtime`
- `modules/product_composition`

This root no longer owns a standalone CMake project, page registry adoption,
screen graph presenter, descriptor registry, or compatibility adapter for final
app shells. Its `CMakeLists.txt` is an archive marker only.

## Archive Layout

- `archive/composition`: old uConsole composition root.
- `archive/adapters`: old legacy implementation adapter.
- `archive/gtk`: old GTK, desktop fallback, and target entrypoint sources.
- `archive/scripts`: old packaging, tools, and CMake presets.
- `archive/tests`: old uConsole smoke/probe files retained for reference.

Do not include files from this archive in final app shells. Move any still-useful
behavior to the final shell, `modules/`, or `platform/` before deleting archived
source.
