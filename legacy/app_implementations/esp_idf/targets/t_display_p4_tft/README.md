# `apps/esp_idf/targets/t_display_p4_tft`

ESP-IDF target descriptor for the `LILYGO T-Display-P4` TFT variant.

Hardware contract:

- display / touch: `HI8561`
- resolution: `540x1168`
- build target: `TRAIL_MATE_IDF_TARGET=t_display_p4_tft`
- build directory: `build.t_display_p4_tft`

Recommended Windows PowerShell workflow:

```powershell
powershell -ExecutionPolicy Bypass -File tools\vscode\run_idf_task.ps1 -Action build   -Target t_display_p4_tft
powershell -ExecutionPolicy Bypass -File tools\vscode\run_idf_task.ps1 -Action flash   -Target t_display_p4_tft -Port COM6
powershell -ExecutionPolicy Bypass -File tools\vscode\run_idf_task.ps1 -Action monitor -Target t_display_p4_tft -Port COM6
```

Raw `idf.py` example:

```bash
idf.py -B build.t_display_p4_tft -DTRAIL_MATE_IDF_TARGET=t_display_p4_tft reconfigure build
idf.py -B build.t_display_p4_tft -DTRAIL_MATE_IDF_TARGET=t_display_p4_tft -p COM6 flash
idf.py -B build.t_display_p4_tft -DTRAIL_MATE_IDF_TARGET=t_display_p4_tft monitor
```

This repo still builds only the P4-side firmware. The external C6 firmware is not bundled here.
