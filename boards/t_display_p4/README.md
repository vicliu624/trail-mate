# `boards/t_display_p4`

Board-owned runtime/profile home for T-Display-P4.

Source reference:

- `.tmp/T-Display-P4`

Current extracted profile:

- Capabilities: display, touch, audio, SDMMC, GPS UART, LoRa
- System I2C: port `0`, `SDA=7`, `SCL=8`
- External I2C: port `1`, `SDA=20`, `SCL=21`
- GPS UART: `UART1`, `TX=22`, `RX=23`
- SDMMC: `D0=39`, `D1=40`, `D2=41`, `D3=42`, `CMD=44`, `CLK=43`
- Audio I2S: `BCLK=12`, `MCLK=13`, `WS=9`, `DOUT=10`, `DIN=11`
- SX1262 SPI: `host=1`, `SCK=2`, `MISO=4`, `MOSI=3`, `CS=24`, `BUSY=6`
- LoRa reset / DIO lines are routed through the XL9535 IO expander in the reference tree
- Boot key: `35`
- IO expander interrupt: `5`
- LCD backlight: `51`

Current responsibilities:

- provide T-Display-P4-specific profile data from `boards/*`
- provide the ESP board-runtime adapter include used by the shared dispatcher
- keep board-local pin/capability knowledge out of generic `platform/esp/boards`
