# `apps/esp_idf/targets/t_display_p4_amoled`

ESP-IDF target descriptor for the `LILYGO T-Display-P4` AMOLED variant.

Hardware contract:

- display: `RM69A10`
- touch: `GT9895`
- resolution: `568x1232`
- build target: `TRAIL_MATE_IDF_TARGET=t_display_p4_amoled`
- build directory: `build.t_display_p4_amoled`

Recommended Windows PowerShell workflow:

```powershell
powershell -ExecutionPolicy Bypass -File tools\vscode\run_idf_task.ps1 -Action build   -Target t_display_p4_amoled
powershell -ExecutionPolicy Bypass -File tools\vscode\run_idf_task.ps1 -Action flash   -Target t_display_p4_amoled -Port COM6
powershell -ExecutionPolicy Bypass -File tools\vscode\run_idf_task.ps1 -Action monitor -Target t_display_p4_amoled -Port COM6
```

Raw `idf.py` example:

```bash
idf.py -B build.t_display_p4_amoled -DTRAIL_MATE_IDF_TARGET=t_display_p4_amoled reconfigure build
idf.py -B build.t_display_p4_amoled -DTRAIL_MATE_IDF_TARGET=t_display_p4_amoled -p COM6 flash
idf.py -B build.t_display_p4_amoled -DTRAIL_MATE_IDF_TARGET=t_display_p4_amoled monitor
```

This repo still builds only the P4-side firmware. The external C6 firmware is not bundled here.
