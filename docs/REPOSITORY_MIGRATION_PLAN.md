#Repository Migration Plan

This document translates the architectural direction into a practical migration path for the current repository.

## Current execution plan

For the current UI/shared-page migration and ESP-IDF compatibility work,
use `docs/UI_SHARED_IDF_PHASE_PLAN.md` as the active execution plan.

That document supersedes any "minimal shell first" interpretation for page migration.
The current rule is:

- one page, one shared implementation
- app layer only owns startup / loop / runtime / adapter composition
- PlatformIO and ESP-IDF must advance together for each migration phase

Current status snapshot:

- phases 0 / 1 / 2 / 3 / 5 / 6 in `docs/UI_SHARED_IDF_PHASE_PLAN.md` are now complete
- phase 4 remains the active workstream for finishing the remaining business-page migrations

## Current authoritative layout

Today, the repository is still primarily organized around a single `src/` tree plus root-level build entrypoints:

- `src/` contains a mix of shared logic, platform logic, UI, board support, and runtime glue
- `platformio.ini` is the main active PlatformIO build entrypoint
- root `CMakeLists.txt` currently acts as a simple IDF-oriented entrypoint
- `variants/`, `boards/`, and board-specific code still reflect the current ESP-oriented structure

This remains the active structure during the migration.

## Target direction

The repository is converging toward:

- `apps/` for application shells
- `modules/` for shared business/core logic
- `platform/` for runtime/framework/hardware adapters

## Migration philosophy

The migration should be incremental.
We should avoid a large one-shot move of the entire `src/` tree.

The repository should remain buildable during each major step.

## Initial mapping guide

The following mapping is directional rather than immediate.
It describes where code is expected to end up over time.

| Current area | Target area | Notes |
| --- | --- | --- |
| `src/chat/domain`, `src/chat/usecase`, `src/chat/ports` | `modules/core_chat` | Shared logic first |
| `src/gps/usecase`, `src/gps/ports` | `modules/core_gps` | Hardware-independent logic first |
| `src/team/domain`, `src/team/protocol`, selected `usecase` logic | `modules/core_team` | Keep transport side effects out |
| `src/hostlink` protocol/state logic | `modules/core_hostlink` | Keep transport adapters out |
| generic `src/sys` utilities | `modules/core_sys` | Filter out framework dependencies |
| `src/board/*` | `platform/esp/boards/*` | Board support belongs in platform layer |
| `src/ble/*` | `platform/esp/...` and later `platform/linux/...` | Split protocol logic from stack integration |
| `src/display/*`, `src/input/*`, `src/usb/*` | `platform/*` | Runtime/framework dependent |
| `src/audio/codec/*` | `platform/esp/arduino_audio_codec/audio/codec/*` | ESP audio codec stack and platform glue |
| root startup / `main.cpp` composition | `apps/*` | App shell responsibility |

## First practical extraction order

### Step 1

Create the new directory skeleton and establish the rules.

### Step 2

Extract `core_sys` as the lowest-risk shared foundation.

### Step 3

Extract `core_chat` and keep storage/network/radio adapters in the platform layer.

### Step 4

Extract `core_gps` while keeping hardware access in adapters.

### Step 5

Extract `core_team` and `core_hostlink`.

### Step 6

Introduce `apps/linux_sim` to validate that extracted shared modules are genuinely portable.

## What not to do during early migration

Do not:

- maintain a second full source tree for IDF long term
- move all UI into a generic shared layer immediately
- copy shared code into Linux-only directories
- let new platform-dependent business logic continue to grow inside `modules/*`

## First concrete migration landed

The first real code migration after the directory skeleton is `TwoPane` UI infrastructure:

- `src/ui/components/two_pane_*` moved into `modules/ui_shared`
- PlatformIO now sees `modules/*` as a library root
- IDF now includes `modules/ui_shared/include` and compiles `modules/ui_shared/src`

This is intentionally small in scope, but it proves the new repository layout can host shared code without duplicating it across separate project trees.

## Second concrete migration landed

The first `core_sys` extraction is now in place:

- `src/sys/ringbuf.h` moved into `modules/core_sys/include/sys/ringbuf.h`
- existing chat code now consumes `sys/ringbuf.h` from `core_sys`
- `event_bus` stays in the legacy tree until its chat/team coupling is split apart

This keeps the migration incremental while starting to establish a real low-level shared foundation.

## Third concrete migration landed

The first `core_chat` extraction is now in place:

- chat `domain` and `ports` headers moved into `modules/core_chat/include`
- selected legacy use-case headers now include module paths directly
- legacy headers under `src/chat/domain` and `src/chat/ports` remain as compatibility shims
- migrated chat domain headers no longer depend on `Arduino.h` for basic integer types

This establishes a real shared contract layer for chat without forcing a one-shot move of chat implementations.

## Fourth concrete migration landed

The next `core_chat` slice is now in place:

- `chat/usecase` public headers moved into `modules/core_chat/include`
- `chat/time_utils.h` moved into `modules/core_chat/include/chat/time_utils.h`
- `chat_model.cpp` and `contact_service.cpp` moved into `modules/core_chat/src`
- `chat_service.cpp` remains in the legacy tree for now because it still publishes through legacy `event_bus`

This means the chat module now contains both shared contracts and part of the real executable use-case/domain implementation.

## Fifth concrete migration landed

`chat_service.cpp` is now decoupled from the legacy event bus:

- `chat_service.cpp` moved into `modules/core_chat/src`
- `ChatService` now exposes a high-level incoming-message observer hook
- a legacy bridge under `src/chat/infra` translates that hook back into `sys::EventBus` events for the current app shell

This is an important boundary shift: shared chat logic no longer needs to know about the legacy global event bus.

## Sixth concrete migration landed

The generic in-memory chat store is now shared:

- `RamStore` moved into `modules/core_chat`
- legacy include path `src/chat/infra/store/ram_store.h` remains as a compatibility shim
- platform-backed stores (`ContactStore`, `FlashStore`, `LogStore`) remain in the legacy tree for now

This keeps the shared module focused on portable chat logic while leaving storage backends with platform dependencies in place until they can be split more cleanly.

## Seventh concrete migration landed

The first `core_gps` slice is now in place:

- portable GPS domain headers moved into `modules/core_gps/include`
- `i_gps_hw.h` moved into `modules/core_gps/include/gps/ports`
- `gps_jitter_filter` moved into `modules/core_gps` as the first shared GPS implementation
- legacy headers remain as compatibility shims

This starts a real shared GPS core without dragging the current board/runtime-specific service layer into the module prematurely.

## Eighth concrete migration landed

The next `core_gps` slice now includes motion policy logic:

- `i_motion_hw.h` moved into `modules/core_gps/include/gps/ports`
- `MotionPolicy` moved into `modules/core_gps`
- the motion hardware contract no longer exposes the Bosch callback typedef directly
- the ESP motion adapter now translates to the generic motion callback type

This is another meaningful boundary improvement because Bosch-specific types are no longer part of the shared GPS contract surface.

## Ninth concrete migration landed

The next `core_gps` slice extracts runtime policy decisions:

- `gps/usecase/gps_runtime_policy.*` moved into `modules/core_gps`
- GPS collection interval normalization is now shared logic
- motion idle-time normalization is now shared logic
- GPS power on/off decision logic is now shared logic
- legacy `GpsService` remains as the FreeRTOS and adapter shell

This keeps the runtime shell in place while moving more GPS behavior into a platform-neutral module boundary.

## Tenth concrete migration landed

The next `core_gps` slice extracts GNSS/NMEA runtime configuration state:

- `gps/usecase/gps_runtime_config.*` moved into `modules/core_gps`
- GNSS config values and pending state are now shared logic
- NMEA config values and pending state are now shared logic
- legacy `GpsService` still owns the actual hardware apply calls

This continues the pattern of shrinking `GpsService` into a runtime shell while moving portable state handling into `core_gps`.

## Eleventh concrete migration landed

The next `core_gps` slice extracts portable runtime state:

- `gps/usecase/gps_runtime_state.*` moved into `modules/core_gps`
- requested GPS collection interval is now shared state
- power strategy and team-mode flags are now shared state
- motion-control enabled / armed state is now shared state
- legacy `GpsService` still owns powered/time-synced flags and task orchestration

This narrows `GpsService` further toward a pure platform runtime shell while preserving behavior.

## Twelfth concrete migration landed

The first `core_team` slice is now in place:

- team `domain` headers moved into `modules/core_team/include`
- team `ports` headers moved into `modules/core_team/include`
- portable team `protocol` headers and codecs moved into `modules/core_team`
- legacy headers under `src/team/domain`, `src/team/ports`, and `src/team/protocol` remain as compatibility shims

This establishes a real shared team contract/protocol layer without pulling the current runtime/event-bus shell into the module too early.

## Chat shell split milestone landed

The remaining legacy `src/chat` tree has now been fully retired from the active build:

- platform-bound chat/runtime shells moved to `platform/esp/arduino_common/include/platform/esp/arduino_common/chat/...` and `platform/esp/arduino_common/src/chat/...`
- platform-neutral chat core remains in `modules/core_chat`
- codebase includes canonical `chat/...` paths instead of `../chat/...` legacy relative paths
- platform-specific chat concrete headers no longer reuse the shared `chat/...` public prefix
- `src/chat` compatibility shims and stub sources have been removed

This is the first point where the repository matches the intended direction more closely: chat is now split between `modules/` shared core and `platform/` runtime shells, instead of being duplicated under a legacy `src/chat` tree.

## Thirteenth concrete migration landed

The next `core_team` slice moves the main team use case into the shared module:

- `team/usecase/team_service.*` moved into `modules/core_team`
- a new `team/ports/i_team_runtime.h` isolates time and random dependencies
- legacy event-bus publishing for unknown app-data now lives in a bridge under `src/team/infra/event`
- legacy Arduino time/random access now lives in a runtime adapter under `src/team/infra/runtime`

This makes the main team service a real shared use case while keeping platform behavior in thin legacy adapters.

## Fourteenth concrete migration landed

The next `core_team` slice moves more use-case logic into the shared module:

- `team/usecase/team_controller.*` moved into `modules/core_team`
- `team/usecase/team_track_sampler.*` moved into `modules/core_team`
- a new `team/ports/i_team_track_source.h` isolates GPS sampling for team track generation
- legacy GPS/time access now lives in runtime and track-source adapters under `src/team/infra/runtime`

This keeps the shared team use-case layer growing while leaving ESP-NOW pairing as a platform-side shell for now.

## Fifteenth concrete migration landed

The first `core_hostlink` slice is now in place:

- `hostlink_types.h` moved into `modules/core_hostlink/include/hostlink`
- `hostlink_codec.h` moved into `modules/core_hostlink/include/hostlink`
- `hostlink_codec.cpp` moved into `modules/core_hostlink/src/hostlink`
- legacy include paths under `src/hostlink` remain as compatibility shims

This establishes a real shared hostlink protocol/codec boundary while keeping USB transport,
FreeRTOS runtime, configuration mutation, GPS polling, and event-bus integration in the legacy shell.

## Sixteenth concrete migration landed

The next `core_hostlink` slice moves more pure hostlink logic into the shared module:

- `hostlink_config_codec.h` moved into `modules/core_hostlink/include/hostlink`
- `hostlink_config_codec.cpp` moved into `modules/core_hostlink/src/hostlink`
- status TLV payload encoding is now shared logic
- config TLV decoding is now shared logic
- legacy `hostlink_config_service.cpp` now acts as the thin shell that gathers runtime state,
  applies decoded config patches to `AppContext`, and performs `settimeofday`

This narrows the hostlink shell further toward transport/runtime integration while keeping
portable protocol mapping in `core_hostlink`.

## Seventeenth concrete migration landed

The next `core_hostlink` slice moves more service-side pure logic into the shared module:

- `hostlink_service_codec.h` moved into `modules/core_hostlink/include/hostlink`
- `hostlink_service_codec.cpp` moved into `modules/core_hostlink/src/hostlink`
- HELLO_ACK payload construction is now shared logic
- GPS event payload construction is now shared logic
- `CmdTxMsg` and `CmdTxAppData` payload parsing is now shared logic

The legacy `hostlink_service.cpp` still owns USB CDC I/O, FreeRTOS queues/tasks, command
execution against app/team services, and periodic scheduling.

## Eighteenth concrete migration landed

The next `core_hostlink` slice extracts hostlink session state bookkeeping:

- `hostlink_session.h` moved into `modules/core_hostlink/include/hostlink`
- `hostlink_session.cpp` moved into `modules/core_hostlink/src/hostlink`
- link state, counters, TX sequence, handshake deadline, and periodic timer state are now shared structs/helpers
- the legacy `hostlink_service.cpp` now uses the shared session helpers for handshake progress,
  periodic status/GPS timing, error bookkeeping, and TX/RX counters

The runtime shell still owns USB transport, queue lifecycle, decoder lifecycle, and command execution.

## Nineteenth concrete migration landed

The next `core_hostlink` slice extracts frame routing and readiness gating:

- `hostlink_frame_router.h` moved into `modules/core_hostlink/include/hostlink`
- `hostlink_frame_router.cpp` moved into `modules/core_hostlink/src/hostlink`
- HELLO handling vs. not-ready rejection is now decided in shared logic
- frame-type to command-kind dispatch is now decided in shared logic

The legacy `hostlink_service.cpp` still executes commands and performs I/O, but it no longer owns
the pure decision logic for how an incoming frame should be routed.

## Immediate repository status after phase 1

After the first phase, the repository may still build exactly as before, but it will have:

- a documented architectural direction
- a target directory skeleton
- a defined migration map
- a clearer place for future extractions

That is intentional.
The first phase is about reducing ambiguity before moving more code.

## Twentieth concrete migration landed

The next `core_hostlink` slice extracts app-data frame encoding helpers:

- `hostlink_app_data_codec.h` moved into `modules/core_hostlink/include/hostlink`
- `hostlink_app_data_codec.cpp` moved into `modules/core_hostlink/src/hostlink`
- RX metadata TLV encoding is now shared logic
- `EvAppData` frame chunking/header assembly is now shared logic

The legacy `hostlink_bridge_radio.cpp` still owns event-bus observation, runtime team-state tracking,
and final event enqueueing, but it no longer owns the pure binary encoding of app-data frames.

## Twenty-first concrete migration landed

The next `core_hostlink` slice extracts more bridge-side pure payload encoding:

- `hostlink_event_codec.h` moved into `modules/core_hostlink/include/hostlink`
- `hostlink_event_codec.cpp` moved into `modules/core_hostlink/src/hostlink`
- `EvRxMsg` payload encoding is now shared logic
- `EvTxResult` payload encoding is now shared logic
- `EvTeamState` payload encoding and payload hashing are now shared logic

The legacy `hostlink_bridge_radio.cpp` still owns event-bus subscription, runtime team-state tracking,
and event selection, but the binary event payload layout is now mostly delegated into `core_hostlink`.

## Twenty-second concrete migration landed

The next `core_chat` slice extracts two Meshtastic pure helpers:

- `chat/infra/meshtastic/mt_region.h` moved into `modules/core_chat/include/chat/infra/meshtastic`
- `chat/infra/meshtastic/mt_region.cpp` moved into `modules/core_chat/src/infra/meshtastic`
- `chat/infra/meshtastic/mt_dedup.h` moved into `modules/core_chat/include/chat/infra/meshtastic`
- `chat/infra/meshtastic/mt_dedup.cpp` moved into `modules/core_chat/src/infra/meshtastic`
- Meshtastic region/frequency estimation is now shared logic
- Meshtastic packet deduplication is now shared logic and no longer depends directly on `Arduino.h`

Legacy headers under `src/chat/infra/meshtastic` remain as compatibility shims, while runtime radio/protobuf
adapter code continues to stay in the legacy shell until more of the protocol layer is separated cleanly.

## Twenty-third concrete migration landed

The next `core_chat` slice extracts a MeshCore pure helper:

- `chat/infra/meshcore/mc_region_presets.h` moved into `modules/core_chat/include/chat/infra/meshcore`
- `chat/infra/meshcore/mc_region_presets.cpp` moved into `modules/core_chat/src/infra/meshcore`
- MeshCore region preset lookup/validation is now shared logic

The active MeshCore adapter still remains in the legacy shell, but its static preset table is now part of the
shared chat protocol layer.

## Twenty-fourth concrete migration landed

The next `core_chat` slice extracts the Meshtastic wire-packet codec:

- `chat/infra/meshtastic/mt_packet_wire.h` moved into `modules/core_chat/include/chat/infra/meshtastic`
- `chat/infra/meshtastic/mt_packet_wire.cpp` moved into `modules/core_chat/src/infra/meshtastic`
- Meshtastic wire header parsing is now shared logic
- Meshtastic AES-CTR packet payload encryption/decryption is now shared logic

The active Meshtastic adapter still remains in the legacy shell, but its on-air wire packet format handling is now
part of the shared chat protocol layer.

## Twenty-fifth concrete migration landed

The next `core_chat` slice extracts the Meshtastic protobuf codec layer:

- `chat/infra/meshtastic/mt_codec_pb.h` moved into `modules/core_chat/include/chat/infra/meshtastic`
- `chat/infra/meshtastic/mt_codec_pb.cpp` moved into `modules/core_chat/src/infra/meshtastic`
- `chat/infra/meshtastic/compression/unishox2.h` moved into `modules/core_chat/include/chat/infra/meshtastic/compression`
- `chat/infra/meshtastic/compression/unishox2.cpp` moved into `modules/core_chat/src/infra/meshtastic/compression`
- Meshtastic protobuf payload encode/decode is now shared logic
- Local text decompression support is now part of the shared chat protocol layer

Legacy headers under `src/chat/infra/meshtastic` remain as compatibility shims, while the runtime Meshtastic adapter still
remains in the legacy shell.

## Twenty-sixth concrete migration landed

The next `core_chat` slice extracts the pure MeshCore identity crypto helpers:

- `chat/infra/meshcore/meshcore_identity_crypto.h` moved into `modules/core_chat/include/chat/infra/meshcore`
- `chat/infra/meshcore/meshcore_identity_crypto.cpp` moved into `modules/core_chat/src/infra/meshcore`
- public-key derivation, signing, verification, and shared-secret derivation are now shared logic
- the legacy `meshcore_identity.cpp` now keeps persistence/random/runtime concerns only
## Twenty-seventh concrete migration landed

The next `core_chat` slice moves the embedded MeshCore `ed25519` implementation under the shared module:

- `chat/infra/meshcore/crypto/ed25519/*` moved into `modules/core_chat/src/infra/meshcore/crypto/ed25519`
- `chat/infra/meshcore/crypto/ed25519/ed_25519.h` is now exposed from `modules/core_chat/include/chat/infra/meshcore/crypto/ed25519`
- the MeshCore crypto helper layer no longer needs to reach back into the legacy `src/` tree for its implementation

## Twenty-eighth concrete migration landed

The next `core_chat` slice extracts shared Meshtastic protocol helpers that were still embedded in the runtime adapter:

- `chat/infra/meshtastic/mt_protocol_helpers.h` moved into `modules/core_chat/include/chat/infra/meshtastic`
- `chat/infra/meshtastic/mt_protocol_helpers.cpp` moved into `modules/core_chat/src/infra/meshtastic`
- ACK gating for broadcast vs. direct sends is now shared logic
- traceroute hop/SNR helper assembly is now shared logic
- protobuf string field draining and encrypted-packet reconstruction are now shared logic
- modem-preset to radio-parameter mapping and channel hashing helpers are now shared logic
- PSK expansion, packet hex formatting, and routing/key-verification log helpers are now shared logic

The active `mt_adapter.cpp` still owns runtime radio I/O, timers, queues, storage, and app/event integration,
but more of its pure protocol behavior now delegates into `core_chat`.
## Twenty-ninth concrete migration landed

The next `core_chat` slice extracts shared MeshCore protocol helpers that were still embedded in the runtime adapter:

- `chat/infra/meshcore/meshcore_protocol_helpers.h` moved into `modules/core_chat/include/chat/infra/meshcore`
- `chat/infra/meshcore/meshcore_protocol_helpers.cpp` moved into `modules/core_chat/src/infra/meshcore`
- MeshCore packet header parsing/building is now shared logic
- MeshCore frame hashing and packet signature calculation are now shared logic
- MeshCore airtime/timeout/delay estimation helpers are now shared logic
- MeshCore key-zero checks, key material expansion, channel hashing, hex formatting, and encrypt/MAC framing helpers are now shared logic

The active `meshcore_adapter.cpp` still owns runtime radio I/O, scheduling, persistence, and event integration,
but more of its pure protocol behavior now delegates into `core_chat`.
## Thirtieth concrete migration landed

The next `core_chat` slice extracts the shared node-store blob format helpers used by the Meshtastic node persistence shell:

- `chat/infra/node_store_blob_format.h` moved into `modules/core_chat/include/chat/infra`
- `chat/infra/node_store_blob_format.cpp` moved into `modules/core_chat/src/infra`
- node-store blob size/count validation is now shared logic
- node-store SD header build/validation is now shared logic
- node-store blob metadata and CRC validation is now shared logic

The legacy `src/chat/infra/meshtastic/node_store.cpp` still owns SD/NVS I/O and fallback policy,
but its blob-format rules now delegate into `core_chat`.

## Thirty-first concrete migration landed

The next cleanup keeps the legacy storage shell thinner without pushing Arduino persistence into `modules/*`:

- `src/chat/infra/blob_store_io.h/.cpp` now centralizes raw SD/Preferences blob load/save helpers
- the legacy `src/chat/infra/contact_store.cpp` now delegates raw blob I/O to that shared shell helper
- this removes duplicated Arduino storage boilerplate while preserving the current ESP-facing persistence boundary

## Thirty-second concrete migration landed

The next cross-module cleanup removes temporary `core_team -> core_chat` shadow headers:

- `modules/core_team/include/team/*` headers now include `core_chat` contracts directly from `modules/core_chat/include/...`
- temporary compatibility headers under `modules/core_team/include/chat/*` are removed
- `core_team` now relies on its declared dependency on `core_chat` instead of re-exporting chat headers locally

This keeps module boundaries cleaner and avoids reintroducing duplicate include trees inside `modules/*`.

## Thirty-third concrete migration landed

The next shell cleanup extends the shared blob-storage helper so more legacy persistence code can collapse around one place:

- `src/chat/infra/blob_store_io.*` now supports optional Preferences-side version/CRC metadata keys
- the legacy `src/chat/infra/meshtastic/node_store.cpp` now delegates its raw NVS blob + metadata I/O to that shared shell helper
- `ContactStore` and `NodeStore` public headers no longer pull `SD.h` / `Preferences.h` into all downstream includes

This keeps platform persistence in the legacy shell while shrinking duplicated storage boilerplate and header coupling.

## Thirty-fourth concrete migration landed

The next `core_chat` slice extracts Meshtastic PKI AES-CCM helpers that were still embedded in the runtime adapter:

- `chat/infra/meshtastic/mt_pki_crypto.h` moved into `modules/core_chat/include/chat/infra/meshtastic`
- `chat/infra/meshtastic/mt_pki_crypto.cpp` moved into `modules/core_chat/src/infra/meshtastic`
- PKI shared-secret hashing is now shared logic
- PKI nonce construction is now shared logic
- PKI AES-CCM encrypt/decrypt helper flow is now shared logic
- PKI key-verification hash derivation is now shared logic

The active `mt_adapter.cpp` still owns Curve25519 key exchange, runtime node-key lookup, and packet routing behavior,
but its pure PKI payload crypto flow now delegates into `core_chat`.

## Thirty-fifth concrete migration landed

The next `core_chat` slice extracts shared MeshCore payload/discovery helpers that were still embedded in the runtime adapter:

- `chat/infra/meshcore/meshcore_payload_helpers.h` moved into `modules/core_chat/include/chat/infra/meshcore`
- `chat/infra/meshcore/meshcore_payload_helpers.cpp` moved into `modules/core_chat/src/infra/meshcore`
- public-channel fallback and channel-key selection helpers are now shared logic
- MeshCore frame/payload shape checks and datagram/frame builders are now shared logic
- direct/group app payload decode, advert/discover decode, node-id derivation, and verification-code formatting are now shared logic

The active `meshcore_adapter.cpp` still owns runtime radio flow, timers, path scheduling, and persistence,
but more of its payload/discovery behavior now delegates into `core_chat`.

## Thirty-sixth concrete migration landed

The next shell cleanup reuses the shared Preferences blob helper for versioned key stores:

- the legacy `meshcore_adapter.cpp` peer-public-key store now uses `blob_store_io` for raw Preferences blob/version handling
- the legacy `mt_adapter.cpp` PKI node-key store now uses `blob_store_io` for raw Preferences blob/version handling
- this keeps versioned Preferences I/O policy in one place while leaving MeshCore/Meshtastic entry validation and migration behavior in their runtime shells

## Thirty-seventh concrete migration landed

The next shell cleanup reuses the shared blob helper for local keypair persistence too:

- the legacy `meshcore_identity.cpp` now uses `blob_store_io` for private/public key Preferences persistence
- the legacy `mt_adapter.cpp` now uses `blob_store_io` for local PKI public/private key Preferences persistence
- this further shrinks direct `Preferences` boilerplate without moving device-local key ownership out of the runtime shells

## Thirty-eighth concrete migration landed

The next `core_chat` slice moves the Meshtastic generated nanopb definitions out of the legacy tree:

- `src/chat/infra/meshtastic/generated/*` moved into `modules/core_chat/src/*` (`meshtastic/*.pb.*` plus shared nanopb support files)
- Meshtastic `.pb.cpp/.h` definitions and shared nanopb support C files now compile as part of `core_chat`
- legacy BLE/UI/runtime code now includes the generated protocol headers through the shared include root instead of reaching into `src/chat/infra/meshtastic/generated` directly

This keeps Meshtastic protocol schema/codegen with the rest of the shared chat protocol layer, while the active runtime adapters still remain in the legacy shell.

## Thirty-ninth concrete migration landed

The next app-shell cleanup restructures `AppContext` without moving platform runtime ownership out of `src/app` yet:

- `AppContext::init()` is decomposed into focused shell helpers for GPS runtime, track recorder, chat store/model, mesh backend setup, team runtime, contact services, and BLE startup
- mesh backend creation is now centralized through one private helper reused by both initial startup and runtime protocol switching
- the public `AppContext` surface remains behavior-compatible, while the bootstrap flow is now easier to split into future `apps/*` entrypoints

This keeps the active Arduino/ESP app composition in the legacy shell, but makes the bootstrap layer substantially easier to untangle in later migration steps.

## Fortieth concrete migration landed

The next `core_chat` slice extracts repeated mesh-protocol utility logic that had started to drift across app, hostlink, BLE, store, and UI shells:

- `chat/infra/mesh_protocol_utils.h` moved into `modules/core_chat/include/chat/infra`
- `chat/infra/mesh_protocol_utils.cpp` moved into `modules/core_chat/src/infra`
- mesh-protocol validity checks, raw-value normalization, full labels, short labels, and slug strings are now shared logic
- legacy app/hostlink/BLE/UI/store code now reuses that helper instead of open-coding protocol validation and display strings

This keeps protocol-selection presentation and normalization rules consistent across shells while staying fully platform-agnostic inside `core_chat`.

## Forty-first concrete migration landed

The next app-shell cleanup extracts shared screen-timeout persistence and normalization logic out of `main.cpp`:

- `src/screen_sleep.cpp` now owns timeout clamping, persisted `Preferences` I/O, and the runtime timeout cache
- `src/screen_sleep.h` now exposes the shared timeout helpers used by main runtime, BLE config sync, and settings UI
- legacy `main.cpp` keeps the actual sleep/screen-saver task behavior, but no longer owns duplicate timeout persistence helpers
- `meshtastic_ble.cpp` now reuses the same timeout clamp/read logic instead of carrying its own copy of the settings constants and preferences reads

This reduces duplication in the ESP shell and makes a later screen/runtime split out of `main.cpp` much more mechanical.

## Forty-second concrete migration landed

The next app-shell cleanup finishes extracting the active screen-sleep runtime out of `main.cpp`:

- `src/screen_sleep.h/.cpp` now own the screen-saver layer, activity mutex, sleep task, and wake/saver state transitions
- `main.cpp` now only provides tiny UI hooks (menu return, unread count, watch-face wake callback) and calls one `initScreenSleepRuntime(...)` entrypoint
- BLE, settings UI, LVGL input paths, and the main runtime now all consume one shared screen-sleep shell API instead of spreading state across multiple translation units

This keeps the ESP-specific runtime in `src/*`, but removes another large stateful subsystem from the startup entrypoint and makes a future `apps/*` split more mechanical.

## Forty-third concrete migration landed

The next app-shell cleanup extracts the application registry out of `main.cpp`:

- `src/ui/app_registry.h/.cpp` now own the `FunctionAppScreen` wrapper plus the board/feature-gated application list
- `main.cpp` no longer carries screen icon declarations, app enter/exit forward declarations, or the large conditional `kAppScreens[]` registry block
- menu construction now reads the registry through `ui::appCatalog()`, leaving `main.cpp` focused on menu layout and runtime wiring

This keeps the active LVGL menu shell behavior unchanged, while making the startup/menu entrypoint smaller and easier to reuse from future `apps/*` targets.

## Forty-fourth concrete migration landed

The next app-shell cleanup extracts low-battery guard behavior out of `main.cpp`:

- `src/app/battery_guard.h/.cpp` now own battery-tier degradation, warning cooldowns, and critical auto-shutdown scheduling
- `main.cpp` now just feeds battery samples into `app::handleLowBattery(...)` during immediate setup and periodic UI refresh
- board-facing shutdown/tier behavior stays in the ESP shell, but no longer sits inline inside the startup/menu entrypoint

This removes another stateful runtime concern from `main.cpp` and makes the remaining startup code more about composition than policy.

## Forty-fifth concrete migration landed

The next app-shell cleanup extracts the menu top-bar/watch-face runtime out of `main.cpp`:

- `src/ui/menu_runtime.h/.cpp` now own the menu top bar, time/battery labels, menu status row registration, periodic refresh timers, and T-Watch watch-face wiring
- `main.cpp` now only provides the shared time-format hook and wires `ui::menu_runtime::init(...)` plus the screen-sleep wake callback
- screen-sleep wake behavior now re-enters through `ui::menu_runtime::onWakeFromSleep()`, keeping watch-face behavior with the rest of the menu runtime instead of the startup entrypoint

This removes another chunk of UI runtime state from `main.cpp` and keeps the startup file focused on composing shells instead of owning their timers and widget trees directly.

## Forty-sixth concrete migration landed

The next app-shell cleanup extracts the board/app bootstrap flow out of `main.cpp`:

- `src/app/bootstrap.h/.cpp` now own wakeup-cause detection helpers, board bring-up, wake-from-sleep board restore, and the board-specific `AppContext::init(...)` wiring
- `main.cpp` now just logs startup state, calls the bootstrap helpers, and continues with LVGL/menu/runtime composition
- board-selection preprocessor branches still exist in the ESP shell, but they are now concentrated in one bootstrap unit instead of being embedded inline in the entrypoint

This makes the remaining `main.cpp` much closer to a pure composition root and reduces one more large block of platform-condition-heavy code.

## Forty-seventh concrete migration landed

The next app-shell cleanup extracts the remaining menu layout/composition block out of `main.cpp`:

- `src/ui/menu_layout.h/.cpp` now own the menu tileview, menu/app panels, LVGL groups, app-button creation, focused-name label updates, and node-id footer creation
- the global UI composition anchors declared in `ui_common.h` (`main_screen`, `menu_g`, `app_g`) are now defined in `menu_layout.cpp` instead of the entrypoint
- `main.cpp` now delegates menu structure creation to `ui::menu_layout::init(...)` and only composes layout, runtime, and screen-sleep shells together

This removes the last large block of menu widget-tree construction from `main.cpp` and further concentrates reusable UI shell code under `src/ui/*`.

## Forty-eighth concrete migration landed

The next app-shell cleanup extracts the last startup wiring helpers out of `main.cpp`:

- `src/app/platform_runtime.h/.cpp` now own the system clock-provider wiring used by `core_sys`
- `src/app/bootstrap.h/.cpp` now also own LVGL/display bring-up, so `main.cpp` no longer includes board-specific headers or calls `beginLvglHelper(...)` directly
- `src/app/ui_runtime.h/.cpp` now own boot-overlay preparation, menu/runtime hook wiring, menu shell initialization, screen-sleep runtime startup, and wake-from-sleep post-init handling
- `src/app/bootstrap.h/.cpp` and `src/app/ui_runtime.h/.cpp` now also hide the direct `AppContext` singleton plumbing from the entrypoint
- `main.cpp` is now reduced to startup logging plus high-level composition of bootstrap and UI shell runtime

This makes the entrypoint much closer to a real composition root and further concentrates ESP-specific startup policy into reusable shell units.

## Forty-ninth concrete migration landed

The next app-shell cleanup extracts the active main loop runtime out of `main.cpp`:

- `src/app/loop_runtime.h/.cpp` now own USB-active loop handling, LVGL cadence, optional timing diagnostics, power-button polling, and `AppContext` ticking
- `main.cpp::loop()` is now only a thin call into `app::loop_runtime::tick()`
- runtime-local LVGL timing state now lives with the loop shell instead of the startup composition root

This removes the last behavior-heavy runtime block from `main.cpp` and leaves the entrypoint as a much cleaner assembly layer for future `apps/*` targets.

## Fiftieth concrete migration landed

The repository now starts using the planned `apps/*` and `platform/*` roots for active runtime code instead of only documenting them:

- `apps/esp_pio` is now a real PlatformIO application shell with its own `startup_runtime` and `loop_runtime`
- `platform/esp/arduino_common` is now a real shared Arduino-on-ESP platform shell with startup-support and display-runtime code
- `src/main.cpp` now delegates directly into `apps/esp_pio/*` instead of pulling startup/loop shells from `src/app`
- `platformio.ini` and `src/CMakeLists.txt` are now wired to compile the new `apps/` and `platform/` runtime roots

This is the first concrete step where the new repository structure is no longer only aspirational: the active runtime shell has started to move out of `src/`.

## Fifty-first concrete migration landed

The ESP Arduino power-policy shell no longer lives under `src/app`:

- `battery_guard.*` moved into `platform/esp/arduino_common`
- `power_tier.*` moved into `platform/esp/arduino_common`
- GPS and UI runtime code now includes the platform power-policy surface instead of `src/app/*`

This keeps low-battery degradation and board power-tier reads with the active ESP runtime shell instead of leaving them in the legacy app bucket.

## Fifty-second concrete migration landed

The Arduino ESP board bootstrap is no longer routed through one catch-all legacy bridge:

- board bring-up, display initialization, and board-specific `AppContext::init(...)` wiring moved into `platform/esp/boards/*`
- `platform/esp/boards` now has a real shared board-runtime surface with per-board implementations for T-Deck, T-Watch S3, and T-LoRa Pager
- `src/platform_bridge/esp_arduino_legacy_bridge.*` is reduced to the remaining temporary `AppContext` / menu-layout accessors

This shrinks the old bridge layer and moves board-instance ownership closer to the platform board shells where it belongs.

## Fifty-third concrete migration landed

The repository now has the first real independent ESP-IDF Tab5 application entry:

- `apps/esp_idf_tab5` now contains its own `startup_runtime` plus an `app_main()` entrypoint
- the IDF-side `src/CMakeLists.txt` now pulls `apps/esp_idf_tab5/src/*` instead of the PlatformIO shell sources
- the legacy Arduino `src/main.cpp` and Arduino-only legacy bridge are now excluded from the IDF component source list

This starts making `apps/esp_idf_tab5` a genuine shell root instead of a documentation-only placeholder.

## Fifty-fourth concrete migration landed

The remaining Arduino PlatformIO `AppContext` bridge logic is no longer parked under `src/platform_bridge`:

- `apps/esp_pio` now owns `initializeAppContext()`, menu-layout initialization, and runtime `AppContext` ticking access through a dedicated `app_runtime_access` helper
- `platform/esp/arduino_common/startup_support` is now reduced to platform startup concerns only (clock providers, wakeup cause, board bring-up)
- the temporary `src/platform_bridge` layer can now be retired from the active PlatformIO shell path

This makes the PlatformIO app shell more self-contained and removes one more legacy cross-layer stopgap from the hot path.

## Fifty-fifth concrete migration landed

The ESP-IDF platform root is no longer empty documentation scaffolding:

- `platform/esp/idf_common` now contains real shared startup code
- `apps/esp_idf_tab5/startup_runtime` now consumes `platform/esp/idf_common::startup_support`
- the IDF-side app shell now has a distinct `apps/* -> platform/*` dependency shape rather than a standalone placeholder file

This is still an early shell, but it is now a real participant in the planned repository structure rather than only a target directory name.

## Fifty-sixth concrete migration landed

The LoRa task runtime is no longer owned by the legacy `src/app` bucket:

- `app_tasks.*` moved into `platform/esp/arduino_common`
- `AppContext` now stops at mesh-layer assembly and no longer starts FreeRTOS radio tasks directly
- `apps/esp_pio/app_runtime_access` now starts the LoRa runtime after `AppContext` initialization succeeds
- shared UI / walkie features now include the platform runtime surface instead of `src/app/app_tasks.h`

This leaves `src/app` focused on application assembly state (`AppContext`, `AppConfig`) and moves one more ESP runtime concern into the planned `platform/*` shell.

## Fifty-seventh concrete migration landed

`AppContext` now owns less ESP-specific runtime wiring directly:

- BLE manager creation and the persisted BLE-enabled startup policy moved out of `AppContext::init(...)` into `apps/esp_pio/app_runtime_access`
- ESP node-id / MAC derivation moved behind a platform helper in `platform/esp/arduino_common`
- Team runtime / GPS track source / ESP-NOW pairing transport construction moved behind a platform bundle helper in `platform/esp/arduino_common`
- `AppContext` now stores Team runtime dependencies via their abstract interfaces instead of concrete Arduino/GPS implementation classes

This further separates application assembly state from ESP shell policy and keeps `src/app` closer to cross-platform composition code.

## Fifty-eighth concrete migration landed

The application runtime frame is now split more cleanly between application state and shell ticking:

- `AppContext::update()` has been split into `updateCoreServices()` and `dispatchPendingEvents(...)`
- BLE manager ticking and UI controller ticking moved out of `AppContext` and into `apps/esp_pio/app_runtime_access`
- `apps/esp_idf_tab5` now has its own `app_runtime_access` and `loop_runtime` shell units instead of a startup banner only
- the IDF app shell now starts its own dedicated loop task, making it a real assembly root for later board/runtime wiring

This keeps the composition root directionally consistent: `src/app` holds application state and orchestration, while `apps/*` own the active runtime frame for each target.

## Fifty-ninth concrete migration landed

The UI / notification event side-effects are no longer hard-coded inside `AppContext`:

- `src/app/app_event_runtime.h` now defines a small runtime-hook contract for shell-specific ticking and event handling
- `AppContext` now exposes `attachEventRuntimeHooks(...)` and `tickEventRuntime()` while keeping core event-state updates local
- `apps/esp_pio/event_runtime` now owns board haptics, toast notifications, team-page event forwarding, and chat UI event forwarding
- `apps/esp_idf_tab5/event_runtime` now mirrors the same hook surface with a placeholder implementation for later board/UI bring-up

This keeps `AppContext` focused on application state transitions while `apps/*` own how each target reacts to those events at runtime.

## Sixtieth concrete migration landed

The runtime event frame is now even less centralized in `AppContext`:

- `hostlink::bridge::on_event(...)` moved out of `AppContext` and into target-specific event runtime hooks
- `apps/esp_pio/event_runtime` now owns both hostlink event observation and UI/notification side-effects for the active Arduino shell
- `apps/esp_idf_tab5/app_runtime_access` now supports binding an `AppContext` and driving the same core/event runtime frame once Tab5 board wiring exists
- `apps/esp_idf_tab5/event_runtime` now mirrors hostlink event observation so the IDF shell has the same composition seam as PlatformIO

This moves another cross-cutting side-effect out of the legacy app bucket and makes the IDF shell a more realistic assembly root instead of only a loop placeholder.

## Sixty-first concrete migration landed

The Tab5 shell is no longer only a documented intention:

- `platform/esp/boards/tab5` now contains a concrete extracted board profile based on `.tmp/M5Tab5-GPS`
- `platform/esp/boards` now exposes `tryResolveAppContextInitHandles(...)`, allowing shells to probe whether a board adapter can really bind `AppContext`
- `apps/esp_idf_tab5` now boots through `platform::esp::boards`, attempts handle resolution, and is ready to call `bindAppContext(...)` once a real Tab5 adapter lands
- the IDF CMake path now includes `platform/esp/boards` so the board runtime surface participates in the target build graph

This makes the Tab5 workstream concrete: the pin map and startup seam now exist in-repo, even though the full `BoardBase` adapter is still the next step.

## Sixty-second concrete migration landed

The board-runtime handle seam is now safe to probe even when a target is still incomplete:

- `AppContextInitHandles` now stores `BoardBase*` instead of a mandatory reference, removing the null-reference placeholder hack from the IDF Tab5 shell
- `apps/esp_pio` and `apps/esp_idf_tab5` now validate resolved board handles explicitly before calling `AppContext::init(...)`
- `platform/esp/boards/tab5` now logs extracted capability flags together with the pin profile derived from `.tmp/M5Tab5-GPS`
- the Tab5 board profile now records board capabilities in one place so later adapters can consume the same source of truth

This keeps the new `apps/* -> platform/*` seam honest: shells can now probe for a board adapter without manufacturing fake references, while Tab5-specific facts continue to move out of the app layer.

## Sixty-third concrete migration landed

The ESP-IDF Tab5 shell no longer drags the legacy Arduino-heavy `src/` tree into its build graph:

- `apps/esp_idf_tab5/app_runtime_access` now probes board/runtime availability without hard-binding to `AppContext`
- `apps/esp_idf_tab5/event_runtime` is now a no-op seam instead of pulling legacy hostlink/UI side-effects into the IDF target
- the IDF component source list now builds only `modules/*` plus `apps/esp_idf_tab5` and `platform/esp/*`
- `src/CMakeLists.txt` now explicitly treats the remaining `src/` tree as out-of-scope for the current IDF target until those pieces are migrated

This is an important boundary correction: the IDF target can now advance as a real `apps/* -> platform/* -> modules/*` consumer instead of inheriting the unfinished Arduino shell by accident.

## Sixty-fourth concrete migration landed

The ESP-IDF Tab5 target now avoids the Arduino-only radio-protocol implementation layer inside `core_chat`:

- the IDF build graph now excludes `modules/core_chat/src/infra/meshcore/*`
- the IDF build graph now excludes `modules/core_chat/src/infra/meshtastic/*`
- generated Meshtastic protobuf transport sources under `modules/core_chat/generated/meshtastic/*` are also left out of the current IDF target
- this leaves the IDF target on shared domain/usecase code while protocol-specific transport and crypto backends are migrated behind cleaner seams

This is intentionally temporary, but directionally correct: protocol implementations that still assume Arduino crypto/runtime facilities should not leak into the new IDF shell by default.

## Sixty-fifth concrete migration landed

The active ESP-IDF component root has now moved out of the legacy `src/` directory:

- the top-level `CMakeLists.txt` now points `EXTRA_COMPONENT_DIRS` at `apps/esp_idf_tab5`
- `apps/esp_idf_tab5/CMakeLists.txt` now owns the IDF build graph for the Tab5 target
- `src/CMakeLists.txt` is no longer the active IDF component root
- this removes the awkward `source component src` behavior and aligns the build layout with the planned `apps/* -> platform/* -> modules/*` structure

This is a structural migration step, not just a build workaround: the IDF target now assembles from its own app root instead of borrowing the legacy source tree's directory identity.

## Sixty-sixth concrete migration landed

The ESP-IDF Tab5 component now registers migrated external sources through `target_sources()` instead of listing them directly in `idf_component_register()`:

- `apps/esp_idf_tab5` now owns a tiny local stub source for IDF component registration
- migrated module/platform sources are attached afterwards via `target_sources(${COMPONENT_LIB} ...)`
- this avoids the fragile IDF source-component inference path that was tripping over external `*/src/*` source trees

This keeps the new app root in control while we are still in the intermediate state where many migrated libraries have not yet become standalone native IDF components.

## Sixty-seventh concrete migration landed

The shared ESP board-runtime switch now resolves the IDF Tab5 board implementation correctly:

- `platform/esp/boards/src/board_runtime.cpp` now includes `sdkconfig.h` when building under ESP-IDF
- the `CONFIG_IDF_TARGET_ESP32P4` check now actually activates the Tab5 board runtime instead of falling through to the Arduino pager implementation

This is a small but important migration fix: without the IDF target config header, the shared board-runtime layer could not reliably select the right board shell under ESP-IDF.

## Sixty-eighth concrete migration landed

The app-shell boundary is now slightly cleaner and no longer forces the ESP-IDF Tab5 target to carry the entire legacy `src/` include root:

- a shared `AppEventRuntimeHooks` contract now lives under `apps/include/apps/app_event_runtime.h` for the new app shells
- `apps/esp_pio` and `apps/esp_idf_tab5` now consume that shared app-shell header instead of including `src/app/app_event_runtime.h` directly
- the ESP-IDF Tab5 component no longer adds `${CMAKE_SOURCE_DIR}/src` as a blanket include directory
- the legacy `src/app/app_event_runtime.h` header remains in place with the same include guard so old and new shells can coexist during migration
- both `pio run -e tlora_pager_sx1262` and `idf.py build` now pass after this boundary reduction

This is still an intermediate state, but it is directionally correct: shared shell contracts start moving under `apps/`, while the new IDF target gets less accidental visibility into the legacy tree.

## Sixty-ninth concrete migration landed

The app event-runtime contract is now a real shared module surface instead of a duplicated app/legacy header pair:

- `AppEventRuntimeHooks` now has a single canonical home at `modules/core_sys/include/app/app_event_runtime.h`
- `src/app/app_context.h`, `apps/esp_pio`, and `apps/esp_idf_tab5` now all include that shared module header directly
- the temporary `apps/include/apps/app_event_runtime.h` copy has been removed
- the legacy compatibility header `src/app/app_event_runtime.h` has been removed as well
- the temporary `apps/include` include-path wiring has been dropped from both the PlatformIO and ESP-IDF shells because it is no longer needed for this contract
- both `pio run -e tlora_pager_sx1262` and `idf.py build` still pass after deleting the legacy entry point

This is the cleaner end-state for this seam: a shared shell contract now lives in `modules/*`, and the migration no longer depends on keeping a duplicated header alive under `src/app`.

## Seventieth concrete migration landed

`AppConfig` is no longer anchored in the legacy `src/app` tree as an inline data-and-persistence monolith:

- the shared configuration data model now lives at `modules/core_sys/include/app/app_config.h`
- `AppConfig` persistence against Arduino `Preferences` moved into the legacy shell adapter `src/app/app_config_store.*`
- `src/app/app_context` and `src/ble/ble_manager.h` now include the shared `app/app_config.h` module header instead of a local legacy header
- the old `src/app/app_config.h` header has been removed
- `core_sys` now declares its public header dependency on `core_chat` and `core_gps`, because `AppConfig` exposes mesh and motion types from those modules
- `AppContext::saveConfig()` is now implemented in `app_context.cpp`, so `app_context.h` no longer needs to inline the persistence adapter call
- both `pio run -e tlora_pager_sx1262` and `idf.py build` pass after this split

This is a meaningful step toward the target structure: shared configuration state now lives in `modules/*`, while `src/app` keeps only the Arduino-specific persistence adapter that still belongs to the legacy shell.

## Seventy-first concrete migration landed

The remaining `src/app` boundary was tightened further without pretending that the legacy application shell is already a shared module:

- `AppContext` no longer stores Arduino `Preferences` by value in its header; it now keeps a `std::unique_ptr<Preferences>` and moves that concrete dependency into `app_context.cpp`
- the Arduino `Preferences` persistence adapter for `AppConfig` has moved out of `src/app` and now lives under `platform/esp/arduino_common/app_config_store.*`
- legacy config migration fallback for old region keys now also lives inside that Arduino persistence adapter instead of leaking into `app_context.cpp`
- `src/app/app_context.h` still includes the public service/interface headers that its API exposes, but it no longer drags in `Preferences.h` just to declare the shell singleton
- `src/ble/ble_manager.h` no longer includes `app/app_config.h` because it only needs `chat::MeshProtocol` in its public interface
- once the header pollution was removed, a couple of UI files (`menu_layout.cpp`, `ui_chat.cpp`) were updated to include `Arduino.h` explicitly instead of depending on transitive includes
- both `pio run -e tlora_pager_sx1262` and `idf.py build` still pass after the cleanup

This finishes the current class of cleanup around `src/app`: the small shared contracts and config model are out, the Arduino persistence adapter is pushed down to the platform layer, and what remains in `src/app` is the legacy application assembly object itself.


## Seventy-second concrete migration landed
- `AppContext` now implements shared facade interfaces from `modules/core_sys/include/app/app_facades.h`.
- the facade split now covers both high-level configuration application (`switchMeshProtocol`) and transport access (`getMeshAdapter`) so service code can stay on narrow interfaces.
- Callers can depend on `IAppConfigFacade` / `IAppMessagingFacade` / `IAppTeamFacade` / `IAppRuntimeFacade` instead of the full `AppContext` header.
- `src/app/app_facade_access.h` provides narrow accessors so runtime and service code can stop including `src/app/app_context.h` unless they really need lifecycle/init APIs.
- `hostlink/*`, `apps/esp_pio/src/event_runtime.cpp`, `src/ui/menu_runtime.cpp`, and `src/walkie/walkie_service.cpp` now use facade access instead of reaching into the singleton directly for simple read/write operations.


## Seventy-third concrete migration landed
- UI aggregate layers now use facade access where they only need config, messaging, team, or runtime state (`ui_status`, `ui_team`, `ui_gps`, `ui_chat`, `ui_contacts`, `ui_usb`, `ui_controller`).
- `IAppRuntimeFacade` now exposes `isBleEnabled()` so top-level status UI no longer depends on `BleManager` or the full `AppContext` header for a simple state query.
- Board-specific pages that still need legacy board handles remain outside the facade for now; they will move later behind platform/runtime adapters instead of bloating `AppContext` again.
- Both `pio run -e tlora_pager_sx1262` and `idf.py build` still pass after this UI-layer cleanup.


## Seventy-fourth concrete migration landed
- Several screen component files now read config, self identity, and contact names through facade access instead of directly including `app_context.h` (`chat_conversation_components`, `chat_message_list_components_watch`, `gps_page_components`, `gps_page_map`, `gps_route_overlay`, `node_info_page_components`).
- This pushes the facade split one level deeper than the top-level `ui_*` aggregators and exposes more hidden include dependencies at the actual screen/component boundary.


## Seventy-fifth concrete migration landed
- Large page modules now depend on `IAppFacade` rather than the concrete `AppContext` singleton where they need a mix of config, messaging, team, and maintenance operations (`settings_page_components`, `contacts_page_components`, `contacts_page_layout`, `team_page_components`, `tracker_page_components`).
- A small `IAppAdminFacade` was added for application-level maintenance actions (`broadcastNodeInfo`, `clearNodeDb`, `clearMessageDb`) so settings flows can stop depending on `AppContext` just for those operations.
- `team_page_input.cpp` no longer includes `app_context.h` at all because it never needed it.

- `menu_layout` now depends only on `IAppMessagingFacade`, so the menu shell can display the node ID without pulling in the concrete `AppContext` type.


## Seventy-sixth concrete migration landed
- `ui_energy_sweep` no longer pulls `AppContext` just to access `LoraBoard` and radio task pause/resume. That responsibility moved into `platform/esp/arduino_common/exclusive_lora_runtime.*`.
- The energy sweep UI now combines `configFacade()` with a dedicated platform radio session adapter: configuration still comes from the app layer, while exclusive hardware ownership comes from the platform layer.
- `ui_walkie_talkie` dropped its direct `TLoRaPagerBoard` precheck and now relies on `walkie::start()` / `walkie::get_last_error()` for readiness reporting, removing another unnecessary concrete board dependency from the UI layer.


## Seventy-seventh concrete migration landed
- `src/walkie/walkie_service.cpp` no longer depends on `TLoRaPagerBoard::getInstance()` or direct board-member access for radio/codec control.
- A dedicated Arduino platform adapter now lives at `platform/esp/arduino_common/walkie_runtime.*`, mirroring the earlier `exclusive_lora_runtime` pattern but for the walkie-talkie FSK/audio session.
- The new walkie runtime owns acquisition of the board handle, radio-task pause/resume coordination, FSK reconfiguration, LoRa restore, and codec I/O/control entry points.
- `walkie_service` now stays focused on session/state-machine logic, packetization, CODEC2 handling, and UI-facing status/error flow instead of reaching into board internals.
- After this step, the remaining direct `AppContext` singleton usage under `src/ui`, `src/walkie`, and `apps/esp_pio` is reduced to the app-shell assembly point in `apps/esp_pio/src/app_runtime_access.cpp`.
- `pio run -e tlora_pager_sx1262` passes after the walkie runtime split, and the touched files are formatted with the CI clang-format rules.


## Seventy-eighth concrete migration landed
- `AppEventRuntimeHooks` now targets `IAppFacade` instead of the concrete `AppContext`, so app-shell event/tick handlers no longer need the legacy application singleton type in their public contract.
- `apps/esp_pio/src/event_runtime.cpp` now works directly against the shared facade surface (`getBoard`, `getBleManager`, `getUiController`, `getContactService`) and drops its concrete `AppContext` dependency.
- A small `IAppLifecycleFacade` was added so the main loop can call `updateCoreServices`, `tickEventRuntime`, and `dispatchPendingEvents` through a narrow interface.
- The PlatformIO shell now pushes concrete `AppContext` usage back into a startup-only binding file: `apps/esp_pio/src/app_context_binding.cpp` owns the singleton/bootstrap path, while `apps/esp_pio/src/app_runtime_access.cpp` stays on facade-level runtime calls.


## Seventy-ninth concrete migration landed
- A BLE-specific facade seam now exists in `modules/core_sys/include/app/app_facades.h`: `IAppBleFacade` extends the shared app facade with the few extra hooks BLE transport still needs (`getNodeStore`, `resetMeshConfig`).
- `src/ble/ble_manager.h`, `src/ble/meshtastic_ble.h`, and `src/ble/meshcore_ble.h` no longer expose `AppContext` in their public API; they now depend on `IAppBleFacade` instead.
- This keeps concrete `AppContext` usage in the startup/binding path while allowing the BLE transport layer to compile against a named interface rather than the legacy singleton type.


## Eightieth concrete migration landed
- `src/chat/infra/meshtastic/mt_adapter.cpp` no longer reaches into `AppContext::getInstance()` just to read `AppConfig`; it now depends on `configFacade()` plus the shared `app/app_config.h` contract.
- This removes one more singleton-style legacy access outside the app shell and keeps the Meshtastic transport closer to the same facade direction already used across UI and runtime code.

## Eighty-first concrete migration landed
- `AppContext` no longer directly constructs the Arduino mesh runtime router or the event-bus chat observer bridge; both now arrive through `AppContextPlatformBindings` factories.
- the `IMeshAdapter` contract now exposes optional backend-host hooks (`installBackend`, `backendProtocol`, `backendForProtocol`), so `src/app` and `src/ble` stop depending on `MeshAdapterRouter` concrete casts.
- `platform/esp/arduino_common` remains the place that creates `MeshAdapterRouter` and `ChatEventBusBridge`, while `apps/esp_pio` injects that platform bundle during startup.

This further shrinks `src/app` toward an application shell/facade layer and keeps runtime/platform concrete chat wiring inside `platform/*` plus `apps/*`.

## Eighty-second concrete migration landed
- `AppContext` no longer owns a `Preferences` instance and no longer directly includes `platform/esp/arduino_common/app_config_store.h`.
- platform config load/save plus message-tone volume lookup now come through `AppContextPlatformBindings`, with Arduino implementations provided by `platform/esp/arduino_common/app_config_store`.
- this keeps NVS/Preferences details inside the platform layer while `src/app` only consumes injected configuration persistence hooks.

This is another small but important step toward making `src/app` a platform-neutral shell instead of a place where ESP persistence APIs leak upward.

## Eighty-third concrete migration landed
- `AppContext` no longer directly constructs `team::infra::TeamCrypto`, `TeamEventBusSink`, `TeamAppDataEventBusBridge`, or `TeamPairingEventBusSink`; those now come through `AppContextPlatformBindings` factories.
- GPS runtime initialization, position-config apply, track-recorder setup, and team-mode propagation now also flow through platform bindings instead of calling `GpsService` / `TrackRecorder` directly from `src/app`.
- `AppContext` now stores team collaborators mostly behind interface types (`ITeamCrypto`, `ITeamEventSink`, `ITeamPairingEventSink`, `UnhandledAppDataObserver`) rather than ESP/event-bus concrete classes.

This keeps more runtime concrete wiring inside `platform/esp/arduino_common` and further reduces `src/app` to an application-shell orchestration layer.

## Eighty-fourth concrete migration landed

- `AppContext::dispatchPendingEvents()` no longer hard-codes chat/contact core event handling; it now acts as a thin event-bus pump that delegates to `AppEventRuntimeHooks`.
- `AppEventRuntimeHooks` gained a `dispatch_event` stage so app-shell runtimes can consume core state-update events before UI/event presentation logic runs.
- `apps/esp_pio/src/event_runtime.cpp` now owns handling of `ChatSendResult`, `NodeInfoUpdate`, `NodeProtocolUpdate`, and `NodePositionUpdate`, keeping the legacy app shell thinner and pushing event policy outward toward `apps/*`.
- This keeps `src/app` closer to orchestration/facade responsibilities while preserving the existing UI/runtime behavior.

## Eighty-fifth concrete migration landed

- The PlatformIO app shell no longer owns the concrete BLE startup restore path or the concrete event-runtime hook implementation.
- Those responsibilities now live under `platform/esp/arduino_common/app_runtime_support.*`, alongside the rest of the Arduino-on-ESP runtime glue.
- `apps/esp_pio/src/app_context_binding.cpp` is thinner again: it now wires `AppContext` using platform-provided helpers instead of directly constructing `BleManager` or owning the event hook code.
- This keeps `apps/*` closer to pure composition while moving one more block of ESP/Arduino runtime behavior into `platform/*`.

## Eighty-sixth concrete migration landed

- `AppContext::updateCoreServices()` no longer hard-codes hostlink/chat/team runtime orchestration.
- `AppEventRuntimeHooks` now also carries an `update_core_services` stage, so app/platform runtime code can own service-update policy alongside tick and event handling.
- The Arduino ESP runtime helper now owns `hostlink::process_pending_commands()`, chat/team incoming processing, team-mode propagation, and team track-sampler updates.
- This trims another chunk of orchestration logic out of `src/app` and keeps `AppContext` focused on facade state plus injected collaborators.

## Eighty-seventh concrete migration landed

- The old scattered team/contact factories in `AppContextPlatformBindings` have been consolidated into higher-level `TeamServicesBundle` and `ContactServicesBundle` factories.
- `AppContext::initTeamServices()` and `AppContext::initContactServices()` now mostly consume preassembled bundles instead of directly constructing service graphs.
- The Arduino ESP platform layer now assembles `TeamService`, `TeamController`, `TeamTrackSampler`, `ContactService`, and the related storage/runtime collaborators on behalf of the app shell.
- This reduces constructor/orchestration noise in `src/app` and moves another slice of concrete startup wiring into `platform/*`.

## Eighty-eighth concrete migration landed

- Startup-finalization logic now has its own `finalize_startup` binding stage in `AppContextPlatformBindings`.
- Team key restore, UI timezone warm-up, and initial team-mode propagation moved out of `AppContext::init()` and into the Arduino ESP startup finalizer.
- The legacy `AppContext::restoreTeamKeys()` helper has been removed, along with the related UI-store/timezone includes from `src/app/app_context.cpp`.
- This trims another non-core startup slice from `src/app` and keeps startup-side runtime policy in the platform/app shell boundary.


## Eighty-ninth concrete migration landed

- Chat startup assembly is now bundle-based in the same style as the earlier team/contact migration.
- `AppContextPlatformBindings` now exposes a higher-level `ChatServicesBundle` factory instead of separate chat-store / mesh-runtime / message-observer factories.
- `AppContext` no longer manually constructs `ChatModel`, `RamStore`, `MeshAdapterRouter`, or `ChatService`; it now consumes a preassembled chat bundle and only applies runtime configuration policy afterwards.
- This further shrinks `src/app` toward orchestration/facade duties while moving concrete chat graph assembly into `platform/*`.

## Ninetieth concrete migration landed

- `src/hostlink` no longer depends on the temporary local `hostlink_codec.h` / `hostlink_types.h` wrapper headers.
- The remaining runtime-side hostlink sources now include canonical `modules/core_hostlink` headers directly.
- The two wrapper headers have been removed, shrinking another small piece of the legacy `src/hostlink` surface.

## Ninety-first concrete migration landed

- `src/app/app_facade_access` no longer hard-binds itself to `AppContext::getInstance()`.
- The facade access layer now works through an explicit `bindAppFacade()` / `unbindAppFacade()` registration step over `IAppFacade`.
- `apps/esp_pio` now binds the concrete facade after successful app-context initialization, which makes the access layer ready for future non-`AppContext` shells.

## Ninety-second concrete migration landed

- `app_facade_access.*` has moved out of `src/app` and into `modules/core_sys` now that it only depends on shared facade interfaces.
- UI, hostlink, walkie, and app-shell callers now include canonical `app/app_facade_access.h` instead of climbing back into `src/app` with relative paths.
- The legacy `src/app/app_facade_access.*` copy has been removed, further shrinking the old app-shell directory.

## Ninety-third concrete migration landed

- `AppContextPlatformBindings` has moved out of `src/app` into `modules/core_sys` as a shared app/platform contract.
- The concrete `AppContext` implementation now lives under `apps/esp_pio`, matching its current role as a PlatformIO app-shell composition root.
- `src/app` is now cleared of concrete sources, which sharpens the repository boundary between shared modules, platform glue, and the active app shell.

## Ninety-fourth concrete migration landed

- The remaining ESP/Arduino hostlink runtime shell (`hostlink_service`, `hostlink_bridge_radio`, and `hostlink_config_service`) has moved out of `src/hostlink` and into `platform/esp/arduino_common`.
- Callers now include canonical platform hostlink headers under `platform/esp/arduino_common/hostlink/*`.
- The protocol reference document moved alongside the shared hostlink module under `modules/core_hostlink/hostlink_protocol.md`.


## Ninety-fifth concrete migration landed

- The remaining ESP/Arduino GPS runtime shell (`gps_service`, `track_recorder`, `gps_service_api`, `gps_hw_status`, `GPS`, and the HAL-backed GPS adapters) has moved out of `src/gps` and into `platform/esp/arduino_common`.
- Callers now include canonical platform GPS runtime headers under `platform/esp/arduino_common/gps/*` and canonical shared GPS headers under `gps/...`.
- The temporary `src/gps` shim headers have been removed, which eliminates another legacy include surface from the repository.


## Ninety-sixth concrete migration landed

- The remaining ESP/Arduino team runtime adapters (`team_crypto`, event-bus bridges/sinks, ESP-NOW pairing transport/service shell, Arduino runtime adapter, and GPS-backed team track source) have moved out of `src/team/infra` and into `platform/esp/arduino_common`.
- Callers now include canonical shared team headers from `team/...` and canonical platform team runtime headers from `platform/esp/arduino_common/team/...`.
- The temporary `src/team` shim headers have been removed, eliminating another legacy include surface from the repository.


## Ninety-seventh concrete migration landed

- The remaining ESP/Arduino HAL wrappers (`hal_gps` and `hal_motion`) have moved out of `src/hal` and into `platform/esp/arduino_common`.
- Callers still include the stable `hal/...` prefix, but that prefix is now served by the platform layer instead of the legacy `src` tree.
- This removes another platform-owned implementation pocket from `src` without changing higher-level GPS service contracts.


## Ninety-eighth concrete migration landed

- The shared ESP board contracts and concrete board implementations have moved out of `src/board` and into `platform/esp/boards`.
- The canonical `board/...` include prefix is preserved, so callers no longer depend on the legacy source-tree location while board ownership is now explicit in the platform layer.
- Board implementation `.cpp` files are now built from the `esp_boards` library, with environment guards keeping non-target boards from hard-failing unrelated builds.


## Ninety-ninth concrete migration landed

- The remaining ESP display abstractions and panel drivers (`DisplayInterface`, `DisplayConfig`, `BrightnessController`, and the ST77xx drivers) have moved out of `src/display` and into `platform/esp/boards`.
- Board-local rotary input has moved with them into the same board platform layer.
- Callers still use stable `display/...` and `input/rotary/...` include prefixes, but those prefixes are now served by the explicit board platform library instead of the legacy `src` tree.


## One hundredth concrete migration landed

- The remaining ESP/Arduino BLE runtime, USB CDC transport, and Morse input runtime have moved out of `src/ble`, `src/usb`, and `src/input` into `platform/esp/arduino_common`.
- Callers now use stable `ble/...`, `usb/...`, and `input/...` include prefixes backed by the platform layer rather than the legacy `src` tree.
- This removes another large runtime/framework-dependent surface from `src` while preserving the existing public include names.


## One hundred first concrete migration landed

- The remaining ESP/Arduino walkie-talkie runtime shell has moved out of `src/walkie` and into `platform/esp/arduino_common`.
- Callers keep using the stable `walkie/...` include prefix, but that prefix is now owned by the platform layer instead of the legacy `src` tree.
- This keeps push-to-talk runtime behavior in the Arduino platform shell while further shrinking the old monolithic source layout.

## One hundred second concrete migration landed

- The remaining ESP/Arduino SSTV runtime and DSP helpers have moved out of `src/sstv`, and `screen_sleep.*` has moved out of `src/`, into `platform/esp/arduino_common`.
- Callers keep using stable `sstv/...` and `screen_sleep.h` include names, but those headers are now served by the platform layer.
- This removes another runtime-heavy feature slice from the legacy `src` tree and makes the `platform/esp/arduino_common` boundary more complete.

## One hundred third concrete migration landed

- The remaining ESP audio codec implementation tree has moved out of `src/audio/codec` into a dedicated platform library at `platform/esp/arduino_audio_codec`.
- Callers keep using the stable `audio/codec/...` include prefix, but that prefix is now owned by an explicit platform package instead of the legacy `src` tree.
- This removes the last non-UI/non-sys platform-heavy pocket from `src` and gives the codec stack a cleaner component boundary than folding it into the generic runtime glue library.

## One hundred fourth concrete migration landed

- The FreeRTOS-backed event bus has moved out of `src/sys` and into `platform/esp/arduino_common` under the stable `sys/event_bus.h` include prefix.
- The temporary `src/sys/clock.h` and `src/sys/ringbuf.h` compatibility shims have been removed, so `sys/clock.h` and `sys/ringbuf.h` now resolve directly from `modules/core_sys`.
- After this step, the legacy `src/sys` directory is fully retired from the active tree.


## One hundred fifth concrete migration landed

- The shared `AppScreen` contract has moved out of `src/ui` into `modules/ui_shared/include/ui/app_screen.h`.
- Shared UI runtime primitives (`set_default_group`, app enter/exit switching, delayed return-to-menu flow, and generic `create_menu`) now live in `modules/ui_shared/include/ui/app_runtime.h` and `modules/ui_shared/src/ui/app_runtime.cpp`.
- `src/ui/ui_common.*` now keeps only product/platform-tinted UI helpers such as battery formatting, timezone persistence, coordinate formatting, and screenshot handling.


## One hundred sixth concrete migration landed

- Pure UI formatters (`ui_format_battery` and `ui_format_coords`) have moved out of `src/ui/ui_common.cpp` into `modules/ui_shared/include/ui/formatters.h` and `modules/ui_shared/src/ui/formatters.cpp`.
- `src/ui/ui_common.*` now focuses more narrowly on board/product-tinted helpers such as top-bar battery reads, timezone persistence, and screenshot-to-SD behavior.
- This keeps LVGL-dependent but platform-neutral formatting logic in the shared UI module instead of the app-local UI shell.


## One hundred seventh concrete migration landed

- The main menu shell (`menu_layout.*` and `menu_runtime.*`) has moved out of `src/ui` and into `apps/esp_pio`, matching its role as PlatformIO app-shell composition code.
- The current app-screen registry has also moved into `apps/esp_pio` as a private implementation detail instead of staying under the legacy shared-looking `src/ui` tree.
- After this step, the active `src/ui` tree is narrower: screen implementations stay there for now, while top-level menu shell composition belongs to the app shell.


## One hundred eighth concrete migration landed

- `ui_boot.*` and `watch_face.*` have moved out of `src/ui` and into `apps/esp_pio` as private app-shell UI helpers.
- These files are only used by the active PlatformIO shell startup/menu flow, so keeping them in `apps/esp_pio` is cleaner than leaving them in the legacy shared-looking UI tree.
- This further narrows `src/ui` toward reusable screen implementations plus still-unmigrated shared/product UI helpers.


## One hundred ninth concrete migration landed

- The menu-status shell (`ui_status.*`) and the app-shell-only top-level page-wrapper files have moved out of `src/ui` and into `apps/esp_pio/src`.
- This includes wrappers such as contacts / GPS / chat / tracker / SSTV / USB / walkie-talkie entrypoints that are only used by the current PlatformIO app registry and loop shell.
- After this step, `src/ui` is more clearly centered on reusable screen implementations, screen helpers, and the remaining shared/product UI runtime pieces that still need a better home.


## One hundred tenth concrete migration landed
- Moved `apps/esp_pio`-owned `ui_controller.*` and `ui_team.*` out of `src/ui` into `apps/esp_pio/src` and rewired include paths to `ui/...` shared headers.
- Introduced `modules/ui_shared/include/ui/chat_ui_runtime.h` so `AppContext`, team screen code, and runtime hooks depend on a small chat UI facade instead of the concrete `UiController` type.
- Split ESP Arduino runtime event wiring into `platform/esp/arduino_common` core hooks plus `apps/esp_pio/src/event_runtime.cpp` so platform code no longer depends directly on app-shell chat/team UI files.


## One hundred eleventh concrete migration landed
- Moved platform-bound UI glue `ui_common.*` and `LV_Helper*` out of `src/ui` into `platform/esp/arduino_common`, keeping the public include surface on canonical `ui/...` paths.
- Moved shared palette constants `ui_theme.h` into `modules/ui_shared/include/ui/ui_theme.h` and rewired remaining screens/widgets to include the shared header.
- Deleted dead `src/ui/compose_flow.h`; it had no implementation and no callers.


## One hundred twelfth concrete migration landed
- Moved shared widgets `top_bar`, `system_notification`, and `toast_widget` from `src/ui/widgets` into `modules/ui_shared`, and rewired callers onto canonical `ui/widgets/...` includes.
- This leaves only device-specific or still-unmigrated widgets under `src/ui/widgets` (`ble_pairing_popup`, `ime`, `map`).


## One hundred thirteenth concrete migration landed
- Moved shared Bluetooth pairing popup and IME widgets (`ble_pairing_popup`, `ime_widget`, `pinyin_ime`, `pinyin_data`) from `src/ui/widgets` into `modules/ui_shared`.
- Rewired all callers onto canonical `ui/widgets/...` includes so app shell, platform BLE glue, and legacy screens no longer depend on `src/ui/widgets` paths for those shared widgets.
- Left `src/ui/widgets/map/*` in place for now because it is still tightly coupled to GPS page state, LVGL image decoding, SD-backed tile storage, and ESP display/SPI coordination.


## One hundred fourteenth concrete migration landed
- Moved `map_tiles` from `src/ui/widgets/map/*` into `platform/esp/arduino_common/ui/widgets/map/*` because it depends on ESP display SPI locking, FreeRTOS timing, Arduino/SD storage, and LVGL-backed tile decode/runtime behavior.
- Kept the public include surface stable at `ui/widgets/map/map_tiles.h`, so GPS and node-info screens no longer rely on a legacy `src/ui/widgets` physical path.
- This clears the remaining files under `src/ui/widgets`; old widget-layer code now lives either in `modules/ui_shared` or `platform/esp/arduino_common`.

## Next IDF convergence note

The repository now has evidence of a second real ESP-IDF board target outside Tab5:

- `.tmp/T-Display-P4` is a pure ESP-IDF device reference tree for an `esp32p4` target
- this means `apps/esp_idf_tab5` should be treated as a transitional shell, not the final IDF app-layer shape
- the intended end-state is `apps/esp_idf/` as the shared IDF shell root, with target descriptors under `apps/esp_idf/targets/tab5` and `apps/esp_idf/targets/t_display_p4`
- board-specific runtime details should continue to converge downward into `boards/tab5` and `boards/t_display_p4`

This reframes the IDF plan from "one board-specific shell" to "one shared IDF shell with multiple board targets".

## Shared IDF runtime units extracted

The first IDF app-shell code has now moved out of the Tab5-specific shell and into the shared `apps/esp_idf` root:

- shared `app_runtime_access`, `event_runtime`, `loop_runtime`, and `startup_runtime` now live under `apps/esp_idf`
- `apps/esp_idf_tab5` now keeps a thin target-specific wrapper layer plus `runtime_config` and `app_main()` entry ownership
- `apps/esp_idf_tab5/CMakeLists.txt` now pulls shared sources from `apps/esp_idf/src/*` while keeping the active IDF component root stable

This is the first concrete code move from a board-specific IDF shell toward a shared-IDF-shell-plus-targets layout.

## One hundred fifteenth concrete migration landed

- the top-level IDF build now selects its active target shell through `TRAIL_MATE_IDF_TARGET` instead of hardwiring `apps/esp_idf_tab5`
- introduced `apps/esp_idf_t_display_p4` as a second transitional IDF target shell so the repository no longer assumes Tab5 is the only IDF board
- changed `platform/esp/boards/src/board_runtime.cpp` to select board runtime by explicit board macro (`TRAIL_MATE_ESP_BOARD_*`) instead of by `CONFIG_IDF_TARGET_ESP32P4`, which would incorrectly collapse multiple ESP32-P4 boards onto the same runtime implementation
- added initial T-Display-P4 target metadata and board-profile placeholders under `apps/esp_idf/targets/t_display_p4`, now converged into `boards/t_display_p4`

This makes the IDF side structurally ready for multiple boards that share the same SoC family.

## One hundred sixteenth concrete migration landed

- `apps/esp_idf` is now the active shared ESP-IDF component root; the top-level `CMakeLists.txt` no longer switches between separate target-specific component directories
- target selection now feeds `SDKCONFIG_DEFAULTS` from `apps/esp_idf/targets/<target>/sdkconfig.defaults`, so target descriptors are part of the real build path rather than documentation only
- shared `app_main()` and `runtime_config` moved into `apps/esp_idf`, removing the need to duplicate wrapper shells for Tab5 and T-Display-P4
- board runtime selection remains explicit through `TRAIL_MATE_IDF_TARGET` -> `TRAIL_MATE_ESP_BOARD_*`, which keeps multiple boards on the same SoC family cleanly separated

This is the first point where `apps/esp_idf` stops being only a shared helper directory and becomes the real IDF app root.

## One hundred seventeenth concrete migration landed

- Moved the reusable main-menu shell pieces back out of `apps/esp_pio` and into `modules/ui_shared`: `menu_profile.*`, `menu_layout.*`, `menu_runtime.*`, `ui_boot.*`, `watch_face.*`, and `ui_status.*`.
- Introduced shared shell contracts (`ui::AppCatalog`, `ui::startup_shell`, and `ui::loop_shell`) so `apps/esp_pio` now mostly assembles Arduino/PlatformIO-specific hooks instead of owning the LVGL shell implementation.
- Dropped the now-unused `apps/esp_pio/include/apps/esp_pio/*` compatibility headers for those menu-shell internals, keeping `apps/esp_pio` focused on entry/runtime composition and app-registry ownership.

This makes the pager / T-Deck / Tab5 menu stack structurally one shared shell with per-device profiles, instead of a PlatformIO-owned menu tree plus board-specific conditionals.

## One hundred eighteenth concrete migration landed

- Folded the default watch-face wiring, screen-sleep hook assembly, and startup completion path into `modules/ui_shared/src/ui/startup_shell.cpp`.
- `apps/esp_pio/src/startup_runtime.cpp` now stays focused on Arduino bring-up order, board/display initialization, `AppContext` bootstrap, and handing a small hook bundle into the shared startup shell.
- Screen-sleep wake handling is now consistently sourced from the shared menu runtime (`ui::menu_runtime::onWakeFromSleep()`), instead of being reassembled in the PlatformIO startup file.

This keeps the product shell startup layer thinner and makes the shared menu/watch-face/screen-sleep behavior easier to reuse from future app targets.

## One hundred nineteenth concrete migration landed

- Introduced `apps/esp_idf`-local `startup_shell` and `loop_shell` so the shared IDF app root now has the same thin-runtime shape as `apps/esp_pio`: runtime entrypoints assemble hooks, while the reusable shell flow lives behind a small contract.
- `apps/esp_idf/src/startup_runtime.cpp` now only wires board bring-up, startup banner logging, AppContext bootstrap, and loop start hooks into `startup_shell`.
- `apps/esp_idf/src/loop_runtime.cpp` now only wires `app_runtime_access::tick()` into `loop_shell`, leaving task creation, delay cadence, and error logging inside the shared IDF loop skeleton.

This keeps target-specific IDF runtime entrypoints small and makes it easier to share future startup/loop behavior across Tab5, T-Display-P4, and later IDF targets.
