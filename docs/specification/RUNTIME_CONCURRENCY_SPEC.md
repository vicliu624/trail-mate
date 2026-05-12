# Runtime and Concurrency Specification

This specification defines the concurrency baseline for Trail Mate targets. It
exists because ESP32, nRF52, Linux, and tests have different runtime mechanics
but must preserve the same ownership model.

## Sources of Concurrency

Trail Mate targets may receive work from:

```text
Radio IRQ
Radio RX task or poll loop
Radio TX completion
GPS UART RX
GPS parser task
BLE stack callback
BLE notify queue
UI event loop
LVGL tick/input
GTK main loop
ASCII terminal input loop
HostLink USB/serial RX
Storage write
Config update
Power/sleep/wake
Timer/retry/ACK timeout
```

## Mandatory Rules

ISR code may only defer.

BLE callbacks must not directly mutate app services.

Radio IRQ handlers must not run protocol or business logic.

GPS tasks must not directly update UI.

UI objects may only be updated on the UI owner thread/task.

Storage backends must declare their concurrency model.

Mutable app-service state must have a single owner context.

Cross-thread and cross-task interaction must use one of:

```text
event queue
command queue
immutable snapshot
declared mutex-protected store
```

## Canonical Event Paths

```yaml
radio_rx:
  path: "radio_irq -> radio_task -> mesh_event_queue -> app_task"
  rule: "IRQ defers; radio_task frames bytes; protocol/app processing runs outside IRQ."

gps_rx:
  path: "uart_irq -> gps_task -> location_event_queue -> app_task"
  rule: "GPS task may parse/normalize but may not directly mutate UI."

ble_write:
  path: "ble_callback -> phone_command_queue -> app_task"
  rule: "BLE stack callback owns transport timing only."

ui_input:
  path: "ui_thread -> presentation_action -> app_command_queue"
  rule: "Renderer translates input into UI actions; app owner executes business mutations."
```

## Forbidden Paths

```text
ble_callback -> ChatService
radio_irq -> MeshSession
gps_task -> lvgl
gtk_worker -> GtkWidget
ui_thread -> blocking storage write
ui_renderer -> direct radio access
ui_renderer -> direct GPS driver access
platform_driver -> direct message policy
```

## UI Thread Only

LVGL:

```text
lv_obj_* may only be called from the LVGL owner task/thread.
Other contexts post UI commands or publish snapshots.
```

GTK:

```text
GtkWidget mutation may only happen on the GTK main loop.
Workers use main-context invocation, channels, or idle callbacks.
```

ASCII/TUI:

```text
Terminal output has one renderer owner.
Input and refresh paths must not concurrently write stdout.
```

Headless:

```text
No renderer owner exists; state is exposed through logs, API, or snapshots.
```

## ISR Policy

ISR code may:

```text
clear interrupt status
record minimal flags
post lightweight ISR-safe events
```

ISR code must not:

```text
malloc/free
write storage
notify BLE
update UI
encode/decode protobuf
send direct messages
parse GPS sentences
perform crypto
```

## Storage Concurrency

Every storage backend must declare:

```text
single writer or multiple writers
reader model
transaction support
async write support
erase/write blocking behavior
required owner context or mutex
```

Examples:

```text
ESP32 NVS: blocking, write-limited, usually mutex protected.
nRF52 flash: erase/write expensive, often async, callback-sensitive.
SQLite: transactional with file locks; must avoid UI-thread blocking.
```

## Mutable State Ownership

Every mutable service must have one owner:

```text
ChatService -> app service context
MeshSession -> mesh/app context declared by target
GpsService -> gps/app context declared by target
ConfigService -> app service context
DeviceStatusService -> app service context
UI State -> UI context or presentation owner
```

Other contexts may send commands, publish events, or consume snapshots.

