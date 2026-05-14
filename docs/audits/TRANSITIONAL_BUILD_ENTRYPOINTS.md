# Transitional Build Entrypoints

## Purpose

This file marks historical build-system-named `apps/` directories that are
allowed during Phase 8 migration but must not be treated as final app shell
semantics.

Core rule:

```text
Build Entrypoint invokes.
App Shell composes.
```

## Transitional Paths

| Path | Transitional role | Final direction | Rule |
| --- | --- | --- | --- |
| `apps/esp_idf` | transitional legacy build entrypoint for ESP-IDF and ESP app/runtime compatibility | `builds/esp_idf` thin wrapper invokes a future ESP LVGL app shell | New code must not treat this as final app shell semantics |
| `apps/esp_pio` | transitional legacy build entrypoint for PlatformIO/Arduino compatibility | `builds/pio_nrf52` thin wrapper invokes a future nRF52 node app shell; legacy ESP PIO compatibility may be split later | New code must not treat this as final app shell semantics |

## Required Marker Language

`apps/esp_idf and apps/esp_pio are transitional historical build entrypoints.`

They are not final product app shells.

## Allowed During Transition

The transitional directories may continue to:

- satisfy existing ESP-IDF and PlatformIO build references
- host compatibility startup glue needed by current build hosts
- provide source paths used by existing CI or developer workflows
- contain migration notes and target descriptors

## Forbidden During Transition

New code must not:

- introduce another build-system-named app shell under `apps/`
- use `apps/esp_idf` as the model for final app shell naming
- use `apps/esp_pio` as the model for final app shell naming
- move build authority deeper into product app code
- assemble Chat/Map/GPS runtime inside `builds/`
- select UX pack inside `builds/`
- define board facts inside `builds/`

## Exit Conditions

`apps/esp_idf` can stop being transitional when:

- `builds/esp_idf` contains the authoritative ESP-IDF wrapper
- the wrapper invokes a product app shell
- existing ESP-IDF target builds are proven through the wrapper
- target descriptors and sdkconfig defaults have a stable final location

`apps/esp_pio` can stop being transitional when:

- `builds/pio_nrf52` contains the authoritative nRF52 PlatformIO wrapper
- legacy ESP PIO compatibility has a documented final path or removal plan
- existing PIO environments are proven through the wrapper
- nRF app shell and board facts are separated enough to rename safely

## Phase 8.2 Non-Action

This marker does not move files or change build behavior. It only prevents
historical compatibility directories from being mistaken for target architecture
truth.
