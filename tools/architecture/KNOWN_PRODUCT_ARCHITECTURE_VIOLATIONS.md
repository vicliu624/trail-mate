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
  state such as `ToRadio`/`FromRadio` buffers. Phase 4.5.3 moved its
  Chat/Team observer registration into `MeshtasticBleObserverBridge`.
- `platform/nrf52/arduino_common/src/ble/meshtastic_ble.cpp` still has the
  same Meshtastic transport compatibility surface for Bluefruit. Phase 4.5.3
  moved its Chat observer registration into `MeshtasticBleObserverBridge` and
  board/settings persistence into `MeshtasticBlePersistenceBridge`.
- `platform/esp/arduino_common/src/ble/meshcore_ble.cpp` still carries legacy
  MeshCore NUS fallback parsing, offline queues, manual contacts, and push
  framing while the shared core coverage is being expanded.
- ESP/nRF52 MeshCore BLE hosts still register Chat/Team observers directly;
  those remain BLE-host thinning follow-ups and should move to bridge objects
  before Phase 5 starts depending on MeshCore BLE status projection.
- `platform/esp/arduino_common/include/ble/meshcore_ble.h` still implements
  `MeshCorePhoneHooks` for BLE-local state such as connection, PIN, telemetry
  mode, and manual contacts.

### Radio runtime and chat protocol adapters are still partially coupled

Existing ESP and nRF52 radio adapter code still lives close to Meshtastic and
MeshCore adapter logic. Later phases should move direct-message, peer-key, PKI,
and protocol mapping decisions out of platform/radio files and into shared
protocol/use-case cores.

Phase 4 introduces the shared `modules/core_mesh` boundary for this split:
`IPacketRadio`, identity/key store ports, `PeerIdentityService`,
`DirectMessageService`, `ReceivePacketService`, `MeshSession`, and initial
Meshtastic/MeshCore protocol strategy shells. The legacy adapters remain known
violations until their behavior is migrated behind those ports/use cases.
Phase 4 PR 4.2/4.3 also adds platform store adapters for Meshtastic-compatible
32-byte local identities and peer public keys:
`EspPreferencesLocalIdentityStore`, `EspPreferencesPeerKeyStore`,
`Nrf52SettingsLocalIdentityStore`, `Nrf52SettingsPeerKeyStore`,
`LinuxSqliteLocalIdentityStore`, and `LinuxSqlitePeerKeyStore`. ESP, nRF52, and
Linux Meshtastic adapters now route PKI identity/key persistence through those
adapters, but their protocol PKI flow, direct encryption/decryption, and runtime
packet behavior are still historical adapter responsibilities until the
protocol strategy and use-case migration slices take over.

Phase 4 PR 4.8 starts thinning the ESP compatibility adapters by routing ESP
Meshtastic non-PKI app-data packet construction through
`MeshtasticProtocolStrategy` and ESP MeshCore direct/group app-data frame
construction through `MeshCoreProtocolStrategy`. Radio TX, route selection,
legacy ACK tracking, PKI compatibility paths, receive parsing, and platform
event bridging remain in the adapters as known follow-up violations until the
full `DirectMessageService`/`ReceivePacketService` bridge can take ownership
without changing on-air behavior.

Phase 4.5 turns this category into a burn-down list rather than a generic
exception. The already-established boundaries are:

- Target manifests now name `core_mesh` as the Meshtastic/MeshCore radio
  profile source and `core_phone` as the BLE phone protocol owner where BLE is
  present.
- Store persistence is wrapped by platform store adapters; stores should not
  decide peer trust, fallback, or key overwrite policy.
- ESP Meshtastic non-PKI app-data packet construction has a
  `MeshtasticProtocolStrategy` path. Phase 4.5.4 routes the actual ESP
  Meshtastic channel-key app-data send path through
  `MeshSession::sendDirect` -> `DirectMessageService` -> `MeshtasticProtocolStrategy`
  -> `IPacketRadio` via `EspMeshtasticAdapterBridge`.
- Phase 4.5.5 routes the actual ESP Meshtastic raw receive entry through
  `EspMeshtasticAdapterBridge::onRadioPacket` -> `ReceivePacketService` ->
  `MeshtasticProtocolStrategy::parseRadioPacket` for shared parse/event
  observation before the legacy compatibility receive logic continues.
- ESP MeshCore direct/group app-data frame construction has a
  `MeshCoreProtocolStrategy` path.

Remaining Phase 4.5 mesh ownership items:

- ESP Meshtastic channel-key app-data direct send now routes through
  `MeshSession::sendDirect` -> `DirectMessageService` -> `MeshtasticProtocolStrategy`
  -> `IPacketRadio`. PKI direct send remains legacy-owned.
- ESP Meshtastic raw receive now also routes through `ReceivePacketService`
  for the shared parse/event path. Legacy receive remains authoritative for
  channel/PKI decrypt, self/drop handling, app queues, ACK/retry, MQTT proxy,
  node info, position, and compatibility event publishing.
- Meshtastic PKI flow, direct encryption/decryption, route selection,
  retransmit/ACK behavior, and decrypted receive routing remain legacy-owned
  until moved behind `core_mesh`.
- MeshCore identity routing, peer-key behavior, direct/group payload
  encryption/decryption, ACK/retry behavior, and receive parsing remain
  legacy-owned until moved behind `core_mesh`.
- nRF52 and Linux active radio paths still need the same MeshSession bridge
  treatment; platform-specific fixes must not add new protocol policy.

Current known LoRa/Mesh exceptions include:

- `platform/esp/arduino_common/src/chat/infra/meshtastic/mt_adapter.cpp`
  still owns Meshtastic PKI flow, direct encryption/decryption, packet
  build/parse, retransmit policy, and radio transmit bridging. PKI persistence
  has been wrapped by platform store adapters, but protocol behavior has not
  moved yet.
- `platform/esp/arduino_common/src/chat/infra/meshcore/meshcore_adapter.cpp`
  still owns MeshCore peer public-key persistence, identity routing, direct
  payload encryption/decryption, frame build/parse, ACK/retry behavior, and
  radio transmit bridging.
- `platform/nrf52/arduino_common/src/chat/infra/meshtastic/meshtastic_radio_adapter.cpp`
  still owns Meshtastic direct-message/PKI behavior, peer key memory, and
  retransmit behavior next to radio I/O. PKI persistence has been wrapped by
  platform store adapters, but protocol behavior has not moved yet.
- `platform/nrf52/arduino_common/src/chat/infra/meshcore/meshcore_radio_adapter.cpp`
  still owns MeshCore direct/group payload framing and identity behavior next
  to radio I/O.
- `platform/linux/common/src/chat/linux_raw_lora_mesh_adapter.cpp` still owns
  Meshtastic direct-message/PKI behavior and packet build/parse next to Linux
  radio I/O. PKI persistence has been wrapped by the Linux store adapter, but
  protocol behavior has not moved yet.

During Phase 4, new direct-send and receive paths should be added to
`modules/core_mesh` first, then the legacy adapters should be thinned into
compatibility shells instead of receiving new protocol-specific policy.

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

Phase 4.5 records the current GPS consumer inventory in
`docs/audits/GPS_CONSUMER_BOUNDARY_AUDIT.md`. New non-UI consumers should use
`ILocationSource`, `ITimeAuthority`, or a device/status snapshot. Existing UI
consumers may stay on `platform/ui/gps_runtime.h` only as Phase 5 replacement
inputs.

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

### Phase 5.6 Chat Presentation Remaining Legacy Ownership

Status: known, bounded.

Phase 5.6 introduced:

- `ui_presentation` chat identity/snapshot/model
- `chat_presentation_adapters` mapper
- `LegacyChatPresentationSource`
- `LegacyChatActionSink`
- partial `ChatUiController` integration

Remaining legacy ownership:

- `ChatUiController` still owns LVGL state machine.
- `ChatUiController` still depends on `ChatService` for event processing and
  compatibility flows.
- Team text projection migrated to `TeamChatPresentationSource`.
- Team text send migrated to `TeamChatActionSink`.
- Team location/command entries are currently projected as textual
  `MessageRow` summaries.
- Team location/command picker remains legacy-owned.
- Team richer payload UI remains future work.
- Team structured pending/failure remains future work.
- Key verification remains legacy-owned.
- Team position picker remains legacy-owned.
- Conversation list cache remains legacy-owned.
- Structured send failure is not yet preserved in `ChatMessage`.

Rules:

- New non-team chat presentation should use `ChatWorkspaceModel`.
- New Team text presentation should use the dedicated Team ChatWorkspaceModel
  backed by `TeamChatPresentationSource` and `TeamChatActionSink`.
- Renderer must not infer pending/failure state.

## Operating Rule

Do not fix these categories opportunistically inside unrelated feature PRs.
When a later phase removes one category, update this file and consider enabling
the relevant checker rule in `--strict` mode for that area.
