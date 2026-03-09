# `modules/core_gps`

Home for shared GPS domain logic, filtering, and portable contracts.

## Migrated now

- `include/gps/domain/gnss_satellite.h`
- `include/gps/domain/gps_state.h`
- `include/gps/domain/motion_sensor_ids.h`
- `include/gps/domain/motion_config.h`
- `include/gps/ports/i_gps_hw.h`
- `include/gps/ports/i_motion_hw.h`
- `include/gps/motion_policy.h`
- `src/motion_policy.cpp`
- `include/gps/usecase/gps_jitter_filter.h`
- `src/usecase/gps_jitter_filter.cpp`
- `include/gps/usecase/gps_runtime_policy.h`
- `src/usecase/gps_runtime_policy.cpp`
- `include/gps/usecase/gps_runtime_config.h`
- `src/usecase/gps_runtime_config.cpp`
- `include/gps/usecase/gps_runtime_state.h`
- `src/usecase/gps_runtime_state.cpp`

## Platform-owned runtime now lives elsewhere

- ESP/Arduino GPS runtime shell now lives under `platform/esp/arduino_common/gps/*`
- That includes `gps_service.*`, `track_recorder.*`, `GPS.*`, `gps_service_api.*`, `gps_hw_status.*`, and the concrete HAL adapters
- These files stay outside `core_gps` because they still depend on FreeRTOS, Arduino, board drivers, SD, display, and ESP runtime policy

## Transitional note

- legacy shim headers under `src/gps/*` have been removed
- callers now include canonical shared headers from `gps/...` and platform runtime headers from `platform/esp/arduino_common/gps/...`
- shared `motion_policy` no longer includes `Arduino.h` directly and now consumes `core_sys` clock helpers
