# `boards/twatchs3`

Board-owned home for T-Watch S3 support.

Current responsibilities:

- own the `boards::twatchs3::TWatchS3Board` implementation
- own T-Watch S3-specific ESP adapter glue under `boards/twatchs3/platform_esp_board_runtime.h`
- keep T-Watch S3 bring-up, pin usage, and singleton/runtime behavior inside `boards/twatchs3/*`

Boundary note:

- shared ESP board abstractions, display helpers, and dispatcher glue stay in `platform/esp/boards`
- T-Watch S3-specific runtime and board knowledge belongs in `boards/twatchs3`
