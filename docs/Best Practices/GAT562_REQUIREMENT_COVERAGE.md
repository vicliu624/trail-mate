# GAT562 需求覆盖清单

本文件用于对照 `GAT562_REQUIREMENTS.md` 跟踪当前实现状态。

## 已落地

- 身份单一事实来源：`long name / short name / node id / BLE 名称` 统一走 `modules/core_chat` 的 `self_identity_policy`
- LoRa 真配置：`region / preset / channel / tx_power / MeshCore radio params` 已能下推到 `SX1262`
- Meshtastic 空口自宣告：共享 `MeshtasticSelfAnnouncementCore`
- MeshCore 空口自宣告：共享 `MeshCoreSelfAnnouncementCore`
- Meshtastic Lite 入站：文本 + 非文本 `AppData`
- MeshCore Lite 入站：advert + direct/group `AppData`
- GNSS 平台 runtime：UART 读流、fix 状态、基础校时
- Device runtime：电池百分比、提示音音量持久化、提示 LED
- 单色 128x64 shared UI 模块：已新增 `modules/ui_mono_128x64`
- GAT562 单色 UI 装配：已接入启动日志、屏保、主菜单、聊天列表、会话、英文/数字/符号输入、身份/无线/设备/GNSS/动作页
- 屏保信息：已按需求提供 `mt/mc`、`MHz` 频率、时间、日期、星期、短 node id
- GAT562 timezone runtime：已新增独立平台时间偏移持久化接口
- no-Team / no-HostLink / no-SD / no-CJK / no-Pinyin：已在 env 与 shared UI 边界裁剪
- `saveConfig()`：已改为“落盘后立即回推运行态”

## 已搭骨架但未闭环

- nRF52 BLE manager：已有协议切换、广播名联动、基础 connectable service
- nRF52 board runtime：已有 3V3 rail / LED / 输入快照 / 频率格式化
- GAT562 app facade：已具备独立装配根，不再依附 ESP app context
- 单色 UI 还未经过编译与实机回归，当前属于“结构已落地、待联调验证”

## 仍待完成

- Meshtastic 手机侧 BLE 完整协议
- MeshCore 手机侧 BLE 完整协议
- 设置页与所有真实能力的最终页面映射收尾
- 启动链 / 主循环 / IC 协调的最终实机验证

## 结论

当前代码已经把“共享身份 / 自宣告 / LoRa 配置 / GNSS / 板级 runtime / no-Team 边界”这几条基础主线重新压回了正确层级，
但 **GAT562 还没有达到需求文档里的“全部完成”状态**。后续最高优先级仍然是：

1. 单色 UI 闭环
2. BLE 手机协议闭环
3. 实机稳定性闭环

---

## 2026-03-18 Incremental Coverage Notes

- nRF52 `Meshtastic BLE` now covers:
  - `get_device_connection_status_request`
  - `get_module_config_request`
  - `set_module_config`
  - `FromRadio.moduleConfig` snapshot emission during `want_config_id`
- nRF52 `MeshCore BLE` now additionally covers:
  - `CMD_GET_BATT_AND_STORAGE`
  - `CMD_SEND_LOGIN`
  - `CMD_SEND_STATUS_REQ`
  - `CMD_SEND_BINARY_REQ`
  - `CMD_SEND_PATH_DISCOVERY_REQ`
  - `CMD_SEND_RAW_DATA`
  - `CMD_SEND_TRACE_PATH`
  - `CMD_SEND_CONTROL_DATA`
  - `CMD_SET_FLOOD_SCOPE`
  - `CMD_HAS_CONNECTION`
  - `CMD_LOGOUT`
  - `CMD_RESET_PATH`
- These additions stay inside `platform/nrf52/arduino_common`, which keeps the board/app/module boundary consistent with `new_hardware_adaptation_prompt.md`.
- No build or device validation is claimed in this note.
- nRF52 `MeshCore BLE` coverage in this round also now includes:
  - `CMD_SEND_TELEMETRY_REQ`
  - `CMD_GET_ADVERT_PATH`
  - `CMD_GET_BATT_AND_STORAGE`
- nRF52 `Meshtastic BLE` in this round also now handles lightweight local admin helpers:
  - canned-message get/set
  - ringtone get/set
  - `set_time_only`
  - `store_ui_config`
  - `remove_by_nodenum` request path
