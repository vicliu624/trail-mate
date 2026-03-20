# `boards/tdeck`

Board-owned home for T-Deck support.

Current responsibilities:

- own the `boards::tdeck::TDeckBoard` implementation
- own T-Deck-specific ESP adapter glue under `boards/tdeck/platform_esp_board_runtime.h`
- keep T-Deck bring-up, pin usage, and singleton/runtime behavior inside `boards/tdeck/*`

Boundary note:

- shared ESP board abstractions, display helpers, and dispatcher glue stay in `platform/esp/boards`
- T-Deck-specific runtime and board knowledge belongs in `boards/tdeck`
