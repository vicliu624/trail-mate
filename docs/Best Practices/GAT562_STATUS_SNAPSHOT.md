# GAT562 Status Snapshot

This snapshot records what the current `gat562_mesh_evb_pro` branch has already aligned with the GAT562 requirements, and what is still intentionally unfinished.

## Aligned in this round

- Shared self identity stays in `modules/core_chat`, including:
  - effective `long_name / short_name / node_id`
  - screen node label formatting
  - BLE visible name derivation
- GAT562 app assembly stays in `apps/gat562_mesh_evb_pro`:
  - startup
  - loop
  - app facade
  - mono UI runtime binding
- nRF52 platform ownership stays in `platform/nrf52/arduino_common`:
  - board runtime
  - input runtime
  - SX1262 packet transport
  - GNSS runtime
  - time persistence
  - BLE manager and protocol service split
- GAT562 env keeps hard product boundaries in `variants/gat562_mesh_evb_pro/envs/gat562_mesh_evb_pro.ini`:
  - no Team
  - no HostLink
  - no SD
  - no CJK
  - no Pinyin IME
  - no `modules/ui_shared/src/*`
- GAT562 app facade still implements Team-related interface slots only as compatibility stubs returning `nullptr`, so Team capability stays structurally excluded instead of half-enabled.

## Newly aligned details

- Mono UI settings now cover real persisted/runtime-backed items:
  - identity
  - protocol
  - tx power
  - region
  - preset
  - channel
  - encrypt
  - PSK or MeshCore channel name
  - BLE
  - time zone
  - GPS on/off
  - GPS interval
  - active chat channel
- BLE enabled state is now treated as configuration state instead of only transient runtime state.
- ESP-side effective identity and BLE visible name now consume the same shared identity policy as GAT.
- nRF52 BLE manager no longer owns protocol-specific service construction logic directly; Meshtastic and MeshCore BLE services now have their own files.
- nRF52 Meshtastic BLE service now moves beyond advertising shell and includes:
  - `ToRadio.packet` text/app-data ingress
  - `want_config_id / heartbeat / disconnect`
  - `MyInfo / self NodeInfo / node-store NodeInfo / Metadata / config-complete`
  - radio-to-phone forwarding for text plus polled app-data
- nRF52 MeshCore BLE service now moves beyond advertising shell and includes:
  - `CMD_DEVICE_QEURY`
  - `CMD_APP_START`
  - `CMD_SEND_TXT_MSG`
  - `CMD_SEND_CHANNEL_TXT_MSG`
  - `CMD_GET_BATT_AND_STORAGE`
  - `CMD_GET_DEVICE_TIME / CMD_SET_DEVICE_TIME`
  - `CMD_GET_CONTACTS`
  - `CMD_GET_CONTACT_BY_KEY`
  - `CMD_SET_ADVERT_NAME`
  - `CMD_SEND_SELF_ADVERT`
  - `CMD_SET_RADIO_PARAMS / CMD_SET_RADIO_TX_POWER`
  - `CMD_GET_CHANNEL / CMD_SET_CHANNEL`
  - `CMD_GET_STATS`
  - `CMD_EXPORT_PRIVATE_KEY / CMD_IMPORT_PRIVATE_KEY`
  - `CMD_SIGN_START / CMD_SIGN_DATA / CMD_SIGN_FINISH`
  - `CMD_REBOOT / CMD_FACTORY_RESET`
  - `CMD_SEND_LOGIN`
  - `CMD_SEND_STATUS_REQ`
  - `CMD_HAS_CONNECTION`
  - `CMD_SEND_BINARY_REQ`
  - `CMD_SEND_PATH_DISCOVERY_REQ`
  - `CMD_SEND_RAW_DATA`
  - `CMD_SEND_TRACE_PATH`
  - `CMD_SEND_TELEMETRY_REQ`
  - `CMD_GET_ADVERT_PATH`
  - `CMD_SET_FLOOD_SCOPE`
  - `CMD_SEND_CONTROL_DATA`
  - `CMD_GET_BATT_AND_STORAGE`
  - `CMD_LOGOUT / CMD_RESET_PATH`
  - incoming text/app-data forwarding on the BLE TX path
- nRF52 `MeshCoreRadioAdapter` now exports its own public key so BLE self-info can stay protocol-backed instead of inventing board-local identity logic.
- nRF52 `MeshCoreRadioAdapter` now exposes self-advert triggering for BLE command routing.
- nRF52 `MeshCoreRadioAdapter` now also owns lightweight MeshCore request/control packet building for:
  - peer request types
  - anonymous login-style requests
  - binary request payloads
  - trace path packets
  - control channel packets
  - flood-scope key storage
- nRF52 Meshtastic BLE admin flow now also answers:
  - `get_device_connection_status_request`
  - correct config echo type for `display` and `device_ui`
- nRF52 Meshtastic BLE now also covers module-config admin/snapshot flow:
  - `get_module_config_request`
  - `set_module_config`
  - `FromRadio.moduleConfig` snapshot stream during `want_config_id`
- nRF52 Meshtastic BLE also now covers lightweight local admin helpers:
  - canned-message get/set
  - ringtone get/set
  - `set_time_only`
  - `store_ui_config`
  - `remove_by_nodenum` as a handled request path

## Still not closed

- nRF52 Meshtastic phone BLE still lacks the fuller official module-config/admin mutation stream.
- nRF52 MeshCore phone BLE still uses lite peer resolution and does not yet claim parity with the richer ESP route/session model.
- The new mono UI and the new nRF52 BLE service split are not build-verified in this round.
- No runtime validation is claimed here.

## Next priority

1. Finish nRF52 Meshtastic BLE protocol behavior on top of the new service split.
2. Finish nRF52 MeshCore BLE protocol behavior on top of the new service split.
3. Build-verify `tdeck` and `gat562_mesh_evb_pro`.
4. Only after build passes, move to device flashing and runtime validation.
