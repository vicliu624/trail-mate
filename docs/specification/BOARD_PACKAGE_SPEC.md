# Board Package Specification

Board packages describe hardware facts and board-level bring-up constraints.
They do not describe business behavior.

## File Location

Board manifests live under:

```text
docs/boards/*.board.yaml
```

The template lives at:

```text
docs/boards/BOARD_MANIFEST_TEMPLATE.yaml
```

## Board Package Owns

```text
pin map
bus map
power rails
display facts
input facts
battery facts
radio hardware facts
GPS hardware facts
board init facts
board sleep/wake hardware hooks
```

## Board Package Must Not Own

```text
ChatService
ContactService
DirectMessageService
MeshtasticPhoneCore
MeshCorePhoneCore
ConfigService business interpretation
UI page state
BLE phone protocol
GPS business strategy
LoRa packet semantics
PKI policy
peer key policy
```

## Board Manifest Semantics

Board manifest answers:

```text
What hardware is physically present?
Which chip is used?
Which bus and pins connect it?
Which power gates exist?
Which display and input devices exist?
Which storage/power features exist?
```

It does not answer:

```text
Which protocols are enabled?
Which UI shell is used?
Which app owns config?
How direct messages are sent?
How BLE phone APIs behave?
How LoRa packets are interpreted?
```

## Target Manifest Difference

```text
board_manifest.yaml describes what hardware exists.
target_manifest.yaml describes how a product uses that hardware.
```

## Allowed Consumers

Board facts may be consumed by:

```text
composition roots
platform factories
board bring-up code
hardware adapter construction
diagnostic tooling
```

Board facts must not be consumed directly by shared app services, protocol
cores, presentation models, or renderers.

