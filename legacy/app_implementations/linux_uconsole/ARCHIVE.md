# uConsole Legacy Archive

This root is archive-only.

- Replacement owner: `apps/linux_uconsole_gtk` plus `modules/ui_gtk_runtime` and
  `modules/product_composition`.
- Do not use from final app shells.
- Do not add active CMake targets here.
- Historical composition sources live under `archive/composition/`.
- Historical adapter sources were deleted after final-shell historical source
  descriptors took over the remaining metadata assertion.
- Historical GTK, desktop, and target sources live under `archive/gtk/`.
- Historical packaging, tools, and presets live under `archive/scripts/`.
- Historical smoke/probe files live under `archive/tests/`.

The active uConsole GTK validation path is `apps/linux_uconsole_gtk`.
