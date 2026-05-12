# Capability and Authority Glossary

This glossary is the required vocabulary for cross-platform Trail Mate
architecture work. Use these terms in documents, PRs, manifests, code reviews,
and migration plans.

## Platform

The chip, operating system, SDK, and runtime environment family.

Examples:

```text
esp32
nrf52
linux
test
```

Platform owns available SDK APIs, threading/task model, ISR model, storage APIs,
time APIs, BLE stack family, filesystem model, and build toolchain.

Platform is not a board and is not a product target.

## Board Variant

A concrete hardware variant with physical facts.

Examples:

```text
t_lora_pager
t_deck
gat562
uconsole_aio2
```

Board Variant owns pin maps, bus maps, display facts, input facts, LoRa chip and
wiring facts, GPS wiring facts, power rails, charger/battery facts, and board
bring-up constraints.

Board Variant must not own product behavior.

## Board Package

The source package and manifest that describe and expose a board variant.

Examples:

```text
boards/tlora_pager
boards/tdeck
boards/gat562_mesh_evb_pro
platform/linux/uconsole
```

The board package may provide hardware facts and board-level bring-up hooks. It
must not interpret protocol semantics or own business services.

## Board Facts

Immutable or slowly changing hardware facts about a board: pins, busses,
display resolution, radio chip, GPS UART, battery/charger presence, and storage
presence.

Board facts answer "what is physically there?" not "what should the product do?"

## Target

A concrete product composition that chooses platform, board, runtime model,
capabilities, authorities, UI shell, storage, protocol enablement, and build
entrypoint.

Examples:

```text
esp-t-lora-pager-lvgl
esp-t-deck-lvgl
nrf52-gat562-node
linux-uconsole-gtk
linux-uconsole-ascii
linux-headless
```

Target chooses. Board describes. Platform adapts.

## Runtime Context

The task, thread, event loop, ISR, queue, lock, and scheduling context in which
work executes.

Examples:

```text
freertos app_task
freertos radio_task
BLE stack callback
GTK main loop
Linux event loop
single-thread test tick
```

Runtime Context is where race policy is enforced.

## Capability

A product-visible ability provided by some board/platform binding.

Examples:

```text
lora
gps
ble
hostlink
storage
display
input
battery
network
audio
map_storage
```

Capability describes ability, state, endpoint host, and link mode. It does not
own business semantics.

## Capability Binding

The declaration that connects a target capability to a board provider, platform
adapter, runtime owner, and protocol consumer.

Example:

```text
target lora capability
  -> board provider t_lora_pager.radio
  -> platform driver esp_sx1262_packet_radio
  -> runtime owner radio_task
  -> protocol consumers meshtastic, meshcore
```

## Authority

The execution host or service that owns mutable truth for a category of state.

Examples:

```text
identity
peer_key_store
node_store
message_store
location
time
config
device_status
ui_state
```

An authority must have one owner. Other contexts use event queues, command
queues, snapshots, or adapters to interact with it.

## Execution Host

The device or process where a runtime object executes.

Examples:

```text
esp32
nrf52
linux
external
test
none
```

Execution Host is not the same thing as platform when multiple hosts cooperate.

## Protocol Core

Shared code that understands protocol wire formats and protocol semantics.

Examples:

```text
Meshtastic radio packet core
Meshtastic phone protocol core
MeshCore phone protocol core
HostLink frame core
NMEA parser
```

Protocol Core must not know platform BLE stacks, USB CDC APIs, board pins, or UI
toolkit objects.

## App Service

Shared or product-level service that coordinates business objects and use cases.

Examples:

```text
ChatService
ContactService
LocationService
ConfigService
DeviceStatusService
```

App Service must not know LVGL, GTK, ASCII, CLI, or board-specific details.

## Presentation Model

UI-independent page/workspace state and actions. It projects app-service
snapshots into a shape that renderers can consume.

Presentation Model may know compact, desktop, terminal, or headless
presentation profiles. It must not contain `lv_obj_t`, `GtkWidget`, terminal
handles, framebuffer handles, or board drivers.

## Renderer

Concrete drawing and toolkit input adapter.

Examples:

```text
LVGL renderer
ASCII canvas renderer
GTK4 renderer
stdout renderer
headless snapshot exporter
```

Renderer consumes snapshots and sends UI actions. It must not own business
state.

## Shell

The runtime surface that hosts one or more renderers and input adapters for a
target.

Examples:

```text
lvgl embedded compact shell
ascii terminal shell
gtk desktop workbench shell
cli command shell
headless API/log/snapshot shell
```

## Command Queue

A queue for requested actions that must be executed by the owner context.

Examples:

```text
ble_callback -> phone_command_queue -> app_task
ui_thread -> app_command_queue -> app_task
```

## Event Queue

A queue for facts that occurred and need to be observed by an owner context.

Examples:

```text
radio_task -> mesh_event_queue -> app_task
gps_task -> location_event_queue -> app_task
```

## Snapshot

An immutable projection of state suitable for cross-context consumption.

UI renderers consume snapshots instead of mutable app-service internals.

## Proxy Mode

The way a local target delegates a capability to another execution host.

Common values:

```text
local_radio
packet_proxy
command_proxy
raw_stream_proxy
fix_proxy
simulated
none
```

Proxy Mode must be declared in target manifests so business code does not infer
it from platform or board names.

## Required Distinction

```text
Platform != Board != Target
```

Use this exact split:

```text
Platform:
  esp32 / nrf52 / linux / test

Board:
  t_lora_pager / t_deck / gat562 / uconsole_aio2

Target:
  esp-t-lora-pager-lvgl
  esp-t-deck-lvgl
  nrf52-gat562-node
  linux-uconsole-gtk
  linux-uconsole-ascii
  linux-headless
```

