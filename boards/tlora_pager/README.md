# `boards/tlora_pager`

Board-owned home for T-LoRa Pager support.

Current responsibilities:

- own the `boards::tlora_pager::TLoRaPagerBoard` implementation
- own T-LoRa Pager-specific ESP adapter glue under `boards/tlora_pager/platform_esp_board_runtime.h`
- keep Pager-specific bring-up, power sequencing, and singleton/runtime behavior inside `boards/tlora_pager/*`

Boundary note:

- shared ESP board abstractions, display/input helpers, and dispatcher glue stay in `platform/esp/boards`
- T-LoRa Pager-specific runtime and board knowledge belongs in `boards/tlora_pager`
