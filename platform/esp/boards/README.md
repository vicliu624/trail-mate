# `platform/esp/boards`

Shared ESP board abstractions, dispatcher glue, and reusable support code.

Current responsibilities:

- shared board-facing contracts under `include/board/*`
- ESP display abstractions and panel drivers under `include/display/*` and `src/display/*`
- board-local input drivers such as rotary handling under `include/input/*` and `src/input/*`
- any remaining shared board implementations under `src/board/*` with public headers in `include/board/*`
- board selection/runtime dispatch under `include/platform/esp/boards/*` and `src/board_runtime.cpp`

Boundary note:

- code that directly owns a specific board's peripherals, display buses, PMU wiring, LoRa wiring, GPS wiring, touch/keyboard/NFC, display panel setup, or board singleton instances belongs under `boards/<name>/*`
- code that is intentionally shared across multiple ESP boards belongs here
- these headers intentionally keep canonical `board/...`, `display/...`, and board-local `input/...` include prefixes so callers no longer depend on legacy `src/*` locations
