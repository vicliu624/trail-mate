# `platform/esp/arduino_audio_codec`

ESP Arduino audio codec platform library.

Current responsibilities:

- ESP audio codec device wrappers and helpers under `audio/codec/*`
- Arduino I2C control transport for codec register access
- ESP I2S / GPIO / codec-device platform bindings
- ES8311 codec device support used by current ESP board implementations

Header boundary rules:

- callers keep using the stable `audio/codec/...` include prefix
- this package owns the concrete ESP/Arduino codec implementation tree
- board/runtime layers depend on this package instead of the legacy `src/audio/codec` location
