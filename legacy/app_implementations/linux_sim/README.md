# Legacy LinuxSim Archive

This directory is an archived historical implementation root.

The active Linux simulator app shell and validation path now live in:

- `apps/linux_sim_shell`
- `modules/ui_ascii_runtime`
- `modules/product_composition`

This root no longer owns a standalone CMake project, simulator runtime entry,
screen graph presenter, descriptor renderer, or compatibility adapter for final
app shells. Its `CMakeLists.txt` is an archive marker only.

## Archive Layout

- `archive/composition`: old simulator composition root.
- `archive/adapters`: removed; old legacy implementation adapter metadata is
  covered by the final-shell historical source descriptor.
- `archive/simulator`: old SDL simulator and target entrypoint.
- `archive/scripts`: old scripts, Docker files, and CMake presets.
- `archive/tests`: old simulator smoke/probe files retained for reference.

Do not include files from this archive in final app shells. Move any still-useful
behavior to the final shell, `modules/`, or `platform/` before deleting archived
source.
