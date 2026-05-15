# `apps/esp_idf`

Common ESP-IDF application shell root.

Purpose:

- hold the shared ESP-IDF app-shell composition that is not tied to a single board
- be the canonical ESP-IDF component root for the repository
- host multiple IDF targets under `targets/` instead of growing one full app shell per board

Dependency shape:

- `apps/esp_idf/*` owns target-independent IDF app assembly
- `apps/esp_idf/targets/<board>/*` owns target metadata such as `sdkconfig.defaults`, partitions, and board selection intent
- `platform/esp/idf_common/*` owns shared IDF platform glue
- `platform/esp/boards/<board>/*` owns board-specific pin/runtime capabilities

Current status:

- this directory is now the active ESP-IDF component root
- board choice is selected by the top-level `TRAIL_MATE_IDF_TARGET` CMake cache variable
- generated `sdkconfig` files now live under the chosen build directory instead of the source root, so `build.tab5` / `build.t_display_p4_tft` / `build.t_display_p4_amoled` do not reuse stale `sdkconfig.<target>` artifacts
- shared `app_main()`, `runtime_config`, `app_registry`, `app_runtime_access`, `app_facade_runtime`, `loop_runtime`, and `startup_runtime` now live here
- `startup_runtime` and `loop_runtime` are now thin adapters; the reusable boot/menu shell lives in `modules/ui_shared/src/ui/startup_shell.cpp` and the loop skeleton lives in `modules/ui_shared/src/ui/loop_shell.cpp`
- `app_runtime_access` is now a thin status facade and uses `platform/esp/idf_common/app_runtime_support` for lifecycle ticking
- IDF now compiles the full shared startup/menu/status runtime instead of the temporary skeleton path
- unsupported capability pages are hidden from the IDF main menu instead of showing placeholder business pages
- `targets/tab5`, `targets/t_display_p4_tft`, and `targets/t_display_p4_amoled` are the real IDF targets wired into this shared shell
- `apps/esp_idf/src` no longer carries page implementations; page code now lives under `modules/ui_shared`
- retired ESP-IDF compatibility stub files have been removed; the component anchor now lives in `src/idf_component_anchor.cpp`

Recommended invocation:

- `idf.py -B build.tab5 -DTRAIL_MATE_IDF_TARGET=tab5 reconfigure build`
- `idf.py -B build.tab5 -DTRAIL_MATE_IDF_TARGET=tab5 -p COM6 flash`
- for Tab5, prefer starting `monitor` separately after a manual `RESET` press instead of chaining `flash monitor`, because auto-reset can leave ESP32-P4 in ROM download mode
- do not rely on a source-root `sdkconfig.tab5`; the build directory now owns the generated config state
- VS Code workspace tasks now provide split `Tab5`, `T-Display-P4 TFT`, and `T-Display-P4 AMOLED` task entries through `tools/vscode/run_idf_task.ps1`
