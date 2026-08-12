# uConsole AIO2 Board Facts

Source:

- `docs/boards/uconsole-aio2.board.yaml`

This record describes Linux-visible hardware and endpoint facts only.

## Identity

- board package: `platform/linux/uconsole`
- board id: `uconsole`
- documented board id: `uconsole_aio2`
- platform family: `linux`

## Confirmed Facts

- display present: yes
- display size: host window or framebuffer
- input keyboard present: yes
- pointer present: yes
- touch state: host dependent
- LoRa endpoint state: optional
- GPS endpoint state: optional
- filesystem: POSIX

## AIO2 binding

- SX1262 is bound to `/dev/spidev1.0` with power `GPIO16`, reset `GPIO25`,
  busy `GPIO24`, and DIO1/IRQ `GPIO26`.
- GPS receiver power is `GPIO27`.
- The ClockworkPi USB CDC serial device is a uConsole control-plane endpoint;
  it must not be auto-selected as GPS. Configure the verified receiver UART in
  **Settings → GPS → Receiver UART** (path and baud are persisted), or supply
  `TRAIL_MATE_GPS_DEVICE` for a headless/operator launch. A saved UI choice
  takes precedence; without one, the operator environment remains intact.
