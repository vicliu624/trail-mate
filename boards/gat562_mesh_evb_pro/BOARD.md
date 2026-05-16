# GAT562 Mesh EVB Pro Board Facts

This board fact record is derived from:

- `docs/boards/gat562.board.yaml`
- `boards/gat562_mesh_evb_pro/include/boards/gat562_mesh_evb_pro/board_profile.h`

It records hardware facts only. It does not choose product presentation,
protocol behavior, debug console behavior, or runtime service.

## Identity

- board package: `boards/gat562_mesh_evb_pro`
- board id used by existing board YAML: `gat562`
- product identity strings in `kBoardProfile`: `GAT562`
- platform family in board YAML: `nrf52`

## Display

- display present: yes
- display size: 128 x 64
- display driver: SSD1306
- display bus: I2C
- I2C address: `0x3C`
- I2C SDA pin: `13`
- I2C SCL pin: `14`

## Input

- touch: no
- keyboard: no
- buttons: yes
- joystick: yes
- primary button pin: `9`
- secondary button pin: `12`
- joystick up/down/left/right/press pins: `28`, `4`, `30`, `31`, `26`

## Radio And GNSS Hardware

- LoRa present: yes
- LoRa chip recorded by board YAML: `sx1262`
- LoRa SPI pins SCK/MISO/MOSI/CS: `43`, `45`, `44`, `42`
- LoRa reset/busy/DIO1/power-enable pins: `38`, `46`, `47`, `37`
- GNSS present: yes
- GNSS UART RX/TX/PPS pins: `15`, `16`, `17`
- GNSS baud rate: `9600`

## Power And Storage

- battery ADC pin: `5`
- peripheral 3V3 enable pin: `34`
- internal flash present: yes
- SD card present: no
- max flash size recorded by `kBoardProfile`: `815104`
- max RAM size recorded by `kBoardProfile`: `248832`
- bootloader settings address recorded by `kBoardProfile`: `0xFF000`

## Non-Facts

These remain outside board facts:

- protocol factory selection
- runtime apply service behavior
- debug console behavior
- presentation page state
- product presentation package selection
- draw path selection
