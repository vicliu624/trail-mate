# Known Product Architecture Violations

This file records historical boundary drift that Phase 1 intentionally does not
fix. The new checker is non-blocking so these can be reduced in later phases
without turning the architecture baseline PR into a behavior-changing rewrite.

## Current Known Categories

### core_sys still contains platform UI contracts

`modules/core_sys/include/platform/ui/*` currently exposes platform-facing UI
runtime contracts from inside a `core_*` module. This violates the new wording
that shared core must not include platform/UI headers. It is historical layout
from the ongoing migration and should be addressed by a later boundary split.

### ESP BLE runtime still carries phone-protocol semantics

The ESP Arduino BLE layer currently includes Meshtastic and MeshCore BLE runtime
logic. Later phases should split BLE host/transport ownership from
MeshtasticPhoneCore and MeshCorePhoneCore protocol semantics.

### Radio runtime and chat protocol adapters are still partially coupled

Existing ESP and nRF52 radio adapter code still lives close to Meshtastic and
MeshCore adapter logic. Later phases should move direct-message, peer-key, PKI,
and protocol mapping decisions out of platform/radio files and into shared
protocol/use-case cores.

### GPS runtime still exposes platform UI service surfaces

Existing GPS runtime APIs are consumed directly by parts of the current UI and
Linux uConsole presentation code. Later phases should route GPS facts through a
LocationService, TimeAuthority, and presentation snapshots.

### LVGL/shared UI code still contains product and platform coupling

Some current LVGL/shared UI files still call app facades and platform UI
runtime APIs directly. Later phases should introduce presentation models that
consume app snapshots and send UI actions without owning business state.

### GTK uConsole workbench still mixes presentation and platform probes

The current uConsole GTK workbench and models still reach into hardware probe
and platform UI runtime APIs. Later phases should separate board/platform
capability probing from GTK rendering and presentation state.

### Target/board preprocessor macros remain outside final composition roots

The current repo still uses board and target macros in build adapters and
runtime selection files. Later phases should keep product choice in explicit
composition roots and target manifests.

## Operating Rule

Do not fix these categories opportunistically inside unrelated feature PRs.
When a later phase removes one category, update this file and consider enabling
the relevant checker rule in `--strict` mode for that area.

