# `platform/esp/arduino_common`

Shared Arduino-on-ESP platform glue.

Current responsibilities:

- clock/provider wiring shared by Arduino-based ESP shells
- board/app bootstrap helpers shared by Arduino-based ESP shells
- LVGL display initialization and cadence runtime shared by Arduino-based ESP shells
- BLE startup restore and app event-runtime hooks shared by Arduino-based ESP shells
- ESP/Arduino-specific chat runtime adapters and storage backends
- ESP/Arduino hostlink runtime shell
- ESP/Arduino GPS runtime shell
- ESP/Arduino team runtime adapters
- ESP/Arduino HAL wrappers
- ESP/Arduino BLE runtime
- ESP/Arduino USB transport
- ESP/Arduino higher-level input runtime such as `input/morse_engine.h`
- ESP/Arduino event bus runtime under `sys/event_bus.h`
- ESP/Arduino walkie-talkie runtime service
- ESP/Arduino SSTV runtime service and DSP helpers
- ESP/Arduino screen-sleep runtime coordination

Header boundary rules:

- shared chat contracts stay in `modules/core_chat/include/chat/...`
- ESP Arduino concrete chat headers live in `platform/esp/arduino_common/include/platform/esp/arduino_common/chat/...`
- ESP Arduino private chat implementation details stay under `platform/esp/arduino_common/src/chat/internal/...`

This keeps `chat/...` reserved for cross-platform module APIs, while platform-specific concrete implementations remain explicitly namespaced under `platform/...`.

Concrete ownership in this layer:

- ESP/Arduino hostlink runtime shell now lives under `include/platform/esp/arduino_common/hostlink/*` and `src/hostlink/*`.
- ESP/Arduino GPS runtime shell now lives under `include/platform/esp/arduino_common/gps/*` and `src/gps/*`.
- ESP/Arduino team runtime adapters now live under `include/platform/esp/arduino_common/team/*` and `src/team/*`.
- ESP/Arduino HAL wrappers now live under `include/hal/*` and `src/hal/*` inside this platform layer.
- ESP/Arduino BLE runtime now lives under `include/ble/*` and `src/ble/*`.
- ESP/Arduino USB transport now lives under `include/usb/*` and `src/usb/*`.
- ESP/Arduino higher-level input runtime such as `input/morse_engine.h` now lives under `include/input/*` and `src/input/*`.
- ESP/Arduino event bus runtime now lives under `include/sys/*` and `src/sys/*`.
- ESP/Arduino walkie runtime now lives under `include/walkie/*` and `src/walkie/*`.
- ESP/Arduino SSTV runtime now lives under `include/sstv/*` and `src/sstv/*`.
- ESP/Arduino screen-sleep runtime now lives under `include/screen_sleep.h` and `src/screen_sleep.cpp`.
