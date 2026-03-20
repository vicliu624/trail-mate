# `boards/tab5`

Board-owned home for Tab5 support and board-specific ESP adapters.

Current extracted board profile and verified M5-Bus routing for the Tab5 target:

- Capabilities: display, touch, audio, SDMMC, GNSS, RS485, M5-Bus LoRa module routing
- System I2C / M5-Bus sensor bus: port `0`, `SDA=31`, `SCL=32`
- Port A / Grove I2C: port `1`, `SDA=53`, `SCL=54`
- Module GNSS UART: `UART2`, `TX=6`, `RX=7`, `38400`
- Module GNSS sensor bus: shared SYS I2C on `31/32`
- RS485 UART: `UART1`, `TX=20`, `RX=21`, `DE=34`
- SDMMC: `D0=39`, `D1=40`, `D2=41`, `D3=42`, `CMD=44`, `CLK=43`
- Audio I2S: `BCLK=27`, `MCLK=30`, `WS=29`, `DOUT=26`, `DIN=28`
- M5-Bus LoRa SPI host: `SPI3_HOST` (`2` in ESP-IDF `spi_host_device_t` enum)
- M5-Bus LoRa SPI: `SCK=5`, `MISO=19`, `MOSI=18`
- M5-Bus LoRa control: `NSS=35`, `RST=45`, `IRQ=16`, `BUSY=-1`, `PWR_EN=-1`
- Power note: M5-Bus modules are powered from the shared external 5V rail; there is no dedicated per-module `PWR_EN` GPIO in the current board profile
- LCD backlight: `22`
- Touch interrupt: `23`

Important routing notes:

- `53/54` are the Tab5 Port A / Grove pins, not the M5-Bus GNSS UART pins.
- Tab5 Module GNSS on M5-Bus uses shared SYS I2C on `31/32` for onboard module sensors and a switched UART route for GNSS NMEA/UBX data.
- The currently adopted GNSS UART route is `TX=6`, `RX=7`, matching the verified M5-Bus routing used in the Tab5 GNSS integration reference.
- The current LoRa route assumes an M5-Bus module wired to `NSS=35`, `IRQ=16`, `RST=45` with fixed SPI on `5/18/19`.

Current responsibilities:

- provide Tab5 board/runtime handles for the selected IDF target shell
- bind `AppContext` into the Tab5 IDF app shell
- keep Tab5-specific pin/capability knowledge in `boards/tab5/board_profile.h`
- own Tab5-specific runtime glue under `boards/tab5/*` rather than `platform/esp/boards/*`
