# `modules/core_sys`

Home for cross-platform system abstractions and generic utilities.

## Migrated now

- `include/sys/ringbuf.h`
- `include/sys/clock.h`
- `include/app/app_facade_access.h`
- `include/app/app_context_platform_bindings.h`
- `src/sys/clock.cpp`
- `src/app/app_facade_access.cpp`

## Moved to platform layer

- `event_bus` now lives at `platform/esp/arduino_common/include/sys/event_bus.h` because it depends on chat/team event payloads plus FreeRTOS runtime details

## Expected future contents

- timer abstraction
- storage and file-system interfaces
- shared utility code that is not tied to ESP or Linux runtime APIs

## Transitional note

- `sys/ringbuf.h` and `sys/clock.h` are now consumed directly from `modules/core_sys/include/sys/*`
- `clock.cpp` provides portable fallbacks based on the C++ standard library and can be overridden by platform-specific providers
- the active app shell should register runtime providers through `sys::set_millis_provider()` and `sys::set_epoch_seconds_provider()` during startup
