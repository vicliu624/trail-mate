# Cross-Platform Product Architecture Specification

This is the top-level architecture baseline for Trail Mate's cross-platform
product family. It does not replace the existing incremental migration. It
defines the shared language, target structure, and review rules that later
LoRa, GPS, BLE, UI, Linux, and embedded work must use.

Phase 1 is architecture baseline work only. It must not extract phone cores,
rewrite adapters, change PKI behavior, move large source trees, or turn target
manifests into a runtime configuration system.

## Core Rule

```text
Target chooses.
Board describes.
Platform adapts.
Runtime schedules.
Capability provides.
Protocol interprets.
UseCase decides.
AppService coordinates.
PresentationModel projects.
Renderer draws.
CompositionRoot wires.
```

In Chinese project terms:

```text
Target decides the product combination.
Board describes hardware facts.
Platform adapts platform capabilities.
Runtime owns scheduling and race boundaries.
Capability provides a device ability.
Protocol interprets protocol semantics.
UseCase performs business decisions.
AppService coordinates business objects.
PresentationModel projects displayable state.
Renderer only draws.
CompositionRoot only assembles dependencies.
```

## Layer Responsibilities

| Layer | Owns | Must not own |
| --- | --- | --- |
| Target | Product combination, selected platform, selected board, UI shell, authorities, capability bindings, race policy | Hardware pin facts, protocol semantics, business code |
| Board | Hardware facts, pin maps, bus maps, electrical constraints, board bring-up facts | Business behavior, protocol interpretation, UI page logic |
| Platform | SDK/OS/framework adapters, driver host integration, storage backend adapters, BLE/serial/SPI/I2C/USB host integration | Meshtastic/MeshCore semantics, direct-message policy, PKI policy, UI state |
| Runtime | Task/thread/event-loop ownership, ISR deferral policy, queue ownership, lock policy | Business decisions or protocol interpretation |
| Capability | Abstract ability exposed by a board/platform binding, such as LoRa, GPS, BLE, storage, display, input | Product policy, protocol semantics, UI layout |
| Protocol | Wire formats, protocol codecs, semantic mapping from protocol frames to domain commands/events | Board pins, BLE stack APIs, UI widgets, storage backend details |
| UseCase | Business decisions and invariants | Platform API calls, renderer code |
| AppService | Coordination of use cases and domain state behind stable app APIs | LVGL, GTK, ASCII, board-specific branches |
| PresentationModel | UI-independent view/workspace state and actions | Toolkit objects, terminal handles, hardware access |
| Renderer | Concrete drawing and input event translation for LVGL, GTK, ASCII, CLI, or headless shell | Business logic and mutable service ownership |
| CompositionRoot | Wires target manifest, board facts, platform factories, app services, presentation models, and renderers | Runtime behavior hidden across random files |

## Canonical Dependency Direction

```text
Renderer
  -> PresentationModel
    -> AppService
      -> UseCase
        -> Domain / Protocol Core / Ports
          <- Platform Adapter

CompositionRoot
  -> Target Manifest
  -> Board Package
  -> Platform Factory
  -> Runtime Context
  -> AppService / PresentationModel / Renderer graph
```

Board facts may be consumed by platform factories and composition roots. Shared
core code must not include board, platform, or UI headers.

## Non-Negotiable Boundaries

Board packages must not know business behavior. A board may say that a radio is
an SX1262 on a given SPI bus; it may not say how direct messages, PKI, contacts,
or phone protocol responses work.

Platform adapters must not interpret product protocol semantics. A BLE host may
own advertising, services, characteristics, callbacks, notify operations, MTU,
chunking, and stack-specific lifecycle. It must not own Meshtastic or MeshCore
phone protocol behavior.

Radio adapters must not own direct message, PKI, peer key, contact, or phone-core
policy. They may send and receive packets and report radio status.

GPS drivers must not directly serve UI, BLE, Mesh, or storage policy. They may
provide bytes, fixes, timestamps, diagnostics, or device status through declared
capabilities.

Renderers must not own business logic. They consume presentation snapshots and
emit UI actions.

App services must not know LVGL, GTK, ASCII, CLI, terminal handles, screen sizes,
font objects, or widget classes.

Protocol cores must not know Bluefruit, NimBLE, BlueZ, USB CDC, UART driver
objects, board pins, or UI toolkit objects.

Shared core modules must not include platform, board, SDK, or UI toolkit headers.

## Target, Board, Platform

These three words are deliberately separate.

Platform is the chip, OS, SDK, and runtime environment family:

```text
esp32
nrf52
linux
test
```

Board Variant is a concrete hardware variant:

```text
t_lora_pager
t_deck
gat562
uconsole_aio2
```

Target is the product composition:

```text
esp-t-lora-pager-lvgl
esp-t-deck-lvgl
nrf52-gat562-node
linux-uconsole-gtk
linux-uconsole-ascii
linux-headless
```

Avoid ambiguous language such as "the ESP32 target" or "the Linux target" when
the actual distinction is platform, board, shell, or product target.

## Capability and Authority

Capabilities describe available abilities such as LoRa, GPS, BLE, storage,
display, input, battery, hostlink, and network.

Authorities describe where mutable truth lives. Examples:

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

Every target manifest must say which execution host owns each authority. If an
authority is proxied, the target must say whether the proxy is packet-level,
command-level, snapshot-level, or disabled.

## Composition Root Rule

Composition roots may contain target-specific decisions. They are the place that
connects board facts to platform adapter factories and app services.

Non-composition-root code must not accumulate product selection macros such as
`BOARD_*`, `TARGET_*`, or `CONFIG_IDF_TARGET*` unless the file is explicitly a
platform build adapter or compatibility wrapper already recorded as a known
violation.

## Phase 1 Scope

Phase 1 adds:

```text
architecture specification
glossary
target manifest specification and drafts
board package specification and drafts
runtime/concurrency specification
UI presentation specification
core_device vocabulary types
PR review checklist
non-blocking boundary report script
known historical violations log
```

Phase 1 intentionally does not:

```text
extract MeshtasticPhoneCore
extract MeshCorePhoneCore
rewrite LoRa adapters
rewrite GPS services
rewrite LVGL pages
add GTK or ASCII UI behavior
fix Linux direct message behavior
change PKI behavior
move large source trees
make target manifests runtime configuration
block CI on the new architecture checker
```

