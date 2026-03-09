# `platform/esp/boards`

Board-specific platform shells and support code for ESP targets.

Current responsibilities:

- shared board-facing contracts under `include/board/*`
- ESP display abstractions and panel drivers under `include/display/*` and `src/display/*`
- board-local input drivers such as rotary handling under `include/input/*` and `src/input/*`
- concrete board implementations under `src/board/*` with public headers in `include/board/*`
- board selection/runtime dispatch under `include/platform/esp/boards/*` and `src/board_runtime.cpp`
- board-specific glue under `tdeck/`, `tlora_pager/`, `twatchs3/`, and `tab5/`

Boundary note:

- code that directly owns ESP board peripherals, display buses, PMU wiring, LoRa wiring, GPS wiring, touch/keyboard/NFC, display panel setup, or board singleton instances belongs here
- these headers intentionally keep canonical `board/...`, `display/...`, and board-local `input/...` include prefixes so callers no longer depend on legacy `src/*` locations
