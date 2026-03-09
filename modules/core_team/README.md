# `modules/core_team`

Home for shared team domain, contracts, and protocol logic.

## Migrated now

- `include/team/ports/i_team_runtime.h`
- `include/team/usecase/team_service.h`
- `src/usecase/team_service.cpp`
- `src/usecase/team_track_sampler.cpp`
- `src/usecase/team_pairing_coordinator.cpp`
- `include/team/usecase/team_track_sampler.h`
- `include/team/ports/i_team_track_source.h`
- `src/usecase/team_controller.cpp`
- `include/team/usecase/team_controller.h`
- `include/team/usecase/team_pairing_service.h`
- `include/team/usecase/team_pairing_coordinator.h`
- `include/team/domain/team_types.h`
- `include/team/domain/team_events.h`
- `include/team/ports/i_team_crypto.h`
- `include/team/ports/i_team_event_sink.h`
- `include/team/ports/i_team_pairing_event_sink.h`
- `include/team/ports/i_team_pairing_transport.h`
- `include/team/protocol/team_wire.h`
- `include/team/protocol/team_mgmt.h`
- `include/team/protocol/team_position.h`
- `include/team/protocol/team_waypoint.h`
- `include/team/protocol/team_chat.h`
- `include/team/protocol/team_track.h`
- `include/team/protocol/team_location_marker.h`
- `include/team/protocol/team_portnum.h`
- `include/team/protocol/team_pairing_wire.h`
- `src/protocol/team_wire.cpp`
- `src/protocol/team_mgmt.cpp`
- `src/protocol/team_position.cpp`
- `src/protocol/team_waypoint.cpp`
- `src/protocol/team_chat.cpp`
- `src/protocol/team_track.cpp`
- `src/protocol/team_location_marker.cpp`
- `src/protocol/team_pairing_wire.cpp`

## Platform-owned runtime now lives elsewhere

- ESP/Arduino team runtime adapters now live under `platform/esp/arduino_common/team/*`
- That includes crypto, event-bus bridges/sinks, ESP-NOW pairing transport/service shell, Arduino runtime clock/random adapter, and GPS-backed track-source adapter
- These files stay outside `core_team` because they still depend on Arduino, ESP-NOW, FreeRTOS, the legacy event bus, and current platform runtime policy

## Transitional note

- legacy shim headers under `src/team/*` have been removed
- callers now include canonical shared headers from `team/...` and platform runtime headers from `platform/esp/arduino_common/team/...`
- `core_team` now includes `core_chat` contracts directly from canonical module paths instead of routing through compatibility wrappers
