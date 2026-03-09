# `modules/core_chat`

Shared chat/domain/protocol core for all targets.

## Layout

```text
modules/core_chat/
  include/chat/
  src/
    domain/
    usecase/
    infra/
  third_party/
    nanopb/
  generated/
    meshtastic/
```

## Notes

- `include/chat/...` is the stable public include surface
- `src/domain`, `src/usecase`, and `src/infra` hold the shared implementation
- `generated/meshtastic` holds generated Meshtastic protobuf sources
- `third_party/nanopb` holds vendored nanopb runtime sources
- platform-bound chat shells live in `platform/esp/arduino_common/src/chat/...`
