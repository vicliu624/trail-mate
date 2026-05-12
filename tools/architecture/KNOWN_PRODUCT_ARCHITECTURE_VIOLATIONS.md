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

### BLE runtime still carries historical phone-protocol transport glue

The ESP and nRF52 BLE hosts now construct `modules/core_phone` cores through an
`AppPhoneFacade`, so GPS/time/config/board reads are no longer owned directly by
the BLE service classes. Historical transport files still contain compatibility
glue around Meshtastic reads/notifies and ESP MeshCore NUS command/event
queues. Later Phase 2 slices should keep reducing these protocol-shaped helpers
or explicitly move them behind phone-core session APIs.

Current known BLE-host exceptions include:

- `platform/esp/arduino_common/src/ble/meshtastic_ble.cpp` still has
  Meshtastic characteristic names, queue names, and read/notify compatibility
  state such as `ToRadio`/`FromRadio` buffers.
- `platform/nrf52/arduino_common/src/ble/meshtastic_ble.cpp` still has the
  same Meshtastic transport compatibility surface for Bluefruit.
- `platform/esp/arduino_common/src/ble/meshcore_ble.cpp` still carries legacy
  MeshCore NUS fallback parsing, offline queues, manual contacts, and push
  framing while the shared core coverage is being expanded.
- `platform/esp/arduino_common/include/ble/meshcore_ble.h` still implements
  `MeshCorePhoneHooks` for BLE-local state such as connection, PIN, telemetry
  mode, and manual contacts.

### Radio runtime and chat protocol adapters are still partially coupled

Existing ESP and nRF52 radio adapter code still lives close to Meshtastic and
MeshCore adapter logic. Later phases should move direct-message, peer-key, PKI,
and protocol mapping decisions out of platform/radio files and into shared
protocol/use-case cores.

### GPS runtime still exposes platform UI service surfaces

Existing GPS runtime APIs are consumed directly by parts of the current UI and
Linux uConsole presentation code. Later phases should route GPS facts through a
LocationService, TimeAuthority, and presentation snapshots.

Phase 3 introduces the shared `core_gps` ports and use cases for this split:
`IGnssByteStream`, `ILocationSource`, `ITimeAuthority`, `NmeaParser`,
`LocationService`, and `TimeAuthority`. ESP BLE and Team track sampling now have
adapter paths that consume `ILocationSource` rather than reading GPS state as
their own domain fact. The older platform/UI GPS runtime remains in place as a
compatibility surface for existing pages and Linux simulator behavior until the
presentation-model phase can replace those direct reads.

Current known GPS/time exceptions include:

- `platform/ui/gps_runtime.h` is still the UI compatibility facade for existing
  LVGL, mono, GTK, and Linux simulator surfaces.
- ESP `GpsService` still owns the current Arduino task shell, receiver config
  application, TinyGPS-backed parsing, and track-recorder append path.
- Some board packages still perform direct `settimeofday` or RTC synchronization
  as historical hardware runtime behavior.
- Host/UI runtime code may still read system time directly for display or legacy
  status until those surfaces consume `ITimeAuthority`/device snapshots.

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
