# `modules/core_hostlink`

Home for shared hostlink protocol logic and state handling.

## Migrated now

- `hostlink_protocol.md`
- `include/hostlink/hostlink_types.h`
- `include/hostlink/hostlink_codec.h`
- `src/hostlink_codec.cpp`
- `include/hostlink/hostlink_config_codec.h`
- `src/hostlink_config_codec.cpp`
- `include/hostlink/hostlink_service_codec.h`
- `src/hostlink_service_codec.cpp`
- `include/hostlink/hostlink_session.h`
- `src/hostlink_session.cpp`
- `include/hostlink/hostlink_frame_router.h`
- `src/hostlink_frame_router.cpp`
- `include/hostlink/hostlink_app_data_codec.h`
- `src/hostlink_app_data_codec.cpp`
- `include/hostlink/hostlink_event_codec.h`
- `src/hostlink_event_codec.cpp`

These pieces are pure protocol/codec logic and do not depend on Arduino, FreeRTOS,
USB, board support, or application composition.

## Deferred for later

- hostlink runtime ownership has moved out of `src/hostlink`, but it still remains in the ESP/Arduino platform layer because it owns USB transport, FreeRTOS tasks/queues, and current runtime scheduling policy

