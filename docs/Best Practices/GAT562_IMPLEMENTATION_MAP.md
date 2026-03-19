# GAT562 实现映射

本文件用于把 `gat562_mesh_evb_pro` 的实现落点、分层归属和当前完成状态写清楚。

权威约束来源：
- `.tmp/meshtastic-firmware` 中 GAT562 硬件参考
- `docs/Best Practices/GAT562_REQUIREMENTS.md`
- `docs/Best Practices/new_hardware_adaptation_prompt.md`

---

## 1. 板级事实

落点：
- `boards/gat562_mesh_evb_pro.json`
- `boards/gat562_mesh_evb_pro/include/boards/gat562_mesh_evb_pro/board_profile.h`
- `variants/gat562_mesh_evb_pro/variant.h`
- `variants/gat562_mesh_evb_pro/variant.cpp`

承载内容：
- nRF52840 / S140 / Flash / RAM / bootloader 约束
- OLED / LoRa / GNSS / LED / 按键 / 电池 / 3V3 供电引脚
- 产品边界：支持 `Meshtastic / MeshCore / BLE / LoRa / GNSS`
- 产品边界：不支持 `Team / HostLink / SD / CJK / 拼音 IME`

原则：
- 板级参数只放在 `boards/` 和 `variants/`
- 业务层不直接写死引脚和硬件事实

---

## 2. 环境定义

落点：
- `variants/gat562_mesh_evb_pro/envs/gat562_mesh_evb_pro.ini`

承载内容：
- GAT562 独立编译环境
- nRF52 include path / generated protobuf include path
- `RadioLib / TinyGPSPlus / nanopb` 依赖
- GAT562 边界裁剪：排除 `Team / HostLink / USB/PC Link / CJK 字库 / 拼音 IME`

原则：
- GAT562 的编译裁剪在环境层完成
- 不用在共享模块里到处写板级 ifdef 兜底

---

## 3. 共享身份与自宣告

落点：
- `modules/core_chat/include/chat/runtime/self_identity_provider.h`
- `modules/core_chat/include/chat/runtime/self_identity_policy.h`
- `modules/core_chat/src/runtime/self_identity_policy.cpp`
- `modules/core_chat/include/chat/runtime/self_announcement_core.h`
- `modules/core_chat/include/chat/runtime/meshtastic_self_announcement_core.h`
- `modules/core_chat/src/runtime/meshtastic_self_announcement_core.cpp`
- `modules/core_chat/include/chat/runtime/meshcore_self_announcement_core.h`
- `modules/core_chat/src/runtime/meshcore_self_announcement_core.cpp`

承载内容：
- `long name / short name / node id / BLE 名称` 的统一派生规则
- Meshtastic NodeInfo 组包
- MeshCore identity advert 组包

原则：
- 身份派生规则属于共享业务，不属于 app
- 空口自宣告规则属于共享业务，不属于板级 runtime

---

## 4. Shared app 容器去 Team 强依赖

落点：
- `modules/core_sys/include/app/app_context_platform_bindings.h`
- `apps/esp_pio/src/app_context.cpp`

承载内容：
- `create_team_services` 不再是 app context 有效性的硬要求
- GAT562 可以作为真实的 “无 Team 设备” 接入，而不是伪造 Team 壳

---

## 5. nRF52 平台桥接与持久化

落点：
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/device_identity.h`
- `platform/nrf52/arduino_common/src/device_identity.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/settings_runtime.h`
- `platform/nrf52/arduino_common/src/settings_runtime.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/self_identity_bridge.h`
- `platform/nrf52/arduino_common/src/self_identity_bridge.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/app_config_store.h`
- `platform/nrf52/arduino_common/src/app_config_store.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/chat/infra/blob_file_store.h`
- `platform/nrf52/arduino_common/src/chat/infra/blob_file_store.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/chat/infra/contact_store.h`
- `platform/nrf52/arduino_common/src/chat/infra/contact_store.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/chat/infra/meshtastic/node_store.h`
- `platform/nrf52/arduino_common/src/chat/infra/meshtastic/node_store.cpp`

承载内容：
- 从 `NRF_FICR->DEVICEADDR` 派生 node id / MAC
- 统一规范化 Meshtastic / MeshCore 配置
- `InternalFS` 上的 app config / 节点 / 联系人存储
- 消息提示音音量持久化入口
- 保存配置后直接回推 radio / BLE / GNSS / identity 运行态

原则：
- 平台层负责“读真值”和“落磁盘”
- 共享层只消费抽象结果

---

## 6. nRF52 LoRa 传输层

落点：
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/chat/infra/radio_packet_io.h`
- `platform/nrf52/arduino_common/src/chat/infra/radio_packet_io.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/chat/infra/sx1262_radio_packet_io.h`
- `platform/nrf52/arduino_common/src/chat/infra/sx1262_radio_packet_io.cpp`

承载内容：
- `IRadioPacketIo` 抽象
- GAT562 SX1262 + RadioLib 实现
- 3V3 / radio power rail / SPI / Radio chip 初始化
- Meshtastic / MeshCore 两套 radio 参数应用
- TX 后回到 RX，RX 时填充 `RSSI / SNR / freq / bw / sf / cr`
- 频率字符串格式化能力在板级 runtime 提供，后续屏保页直接消费

原则：
- radio 真实参数生效属于平台层
- app 只负责把当前协议配置下发给平台层

---

## 7. nRF52 协议适配器

落点：
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/chat/infra/protocol_factory.h`
- `platform/nrf52/arduino_common/src/chat/infra/protocol_factory.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/chat/infra/meshtastic/meshtastic_radio_adapter.h`
- `platform/nrf52/arduino_common/src/chat/infra/meshtastic/meshtastic_radio_adapter.cpp`
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/chat/infra/meshcore/meshcore_radio_adapter.h`
- `platform/nrf52/arduino_common/src/chat/infra/meshcore/meshcore_radio_adapter.cpp`

承载内容：
- Meshtastic Lite Adapter：文本、AppData、NodeInfo、自宣告
- MeshCore Lite Adapter：文本/AppData 转发、identity advert、自宣告
- Meshtastic 收包按 `channel_hash -> key` 解密，不再只支持无 PSK

原则：
- 协议逻辑尽量复用 `modules/core_chat`
- 平台适配器只补 transport / runtime glue

---

## 8. nRF52 GNSS / Device runtime

落点：
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/gps_runtime.h`
- `platform/nrf52/arduino_common/src/platform_ui_gps_runtime.cpp`
- `platform/nrf52/arduino_common/src/platform_ui_device_runtime.cpp`
- `platform/nrf52/arduino_common/src/platform_ui_time_runtime.cpp`

承载内容：
- `platform::ui::gps::*` 的 nRF52 实现
- `platform::ui::device::*` 的 nRF52 实现
- UART GNSS 输入解析
- 基础电池百分比读取
- GPS 校时入口
- 本地时区偏移持久化与本地时间换算
- 统一走 `board_runtime` 完成 3V3 rail / LED / 输入引脚初始化

原则：
- UI 读的是共享平台 runtime 接口
- 具体串口、ADC、RTC 判定都归平台层

---

## 9. nRF52 BLE runtime

落点：
- `platform/nrf52/arduino_common/include/ble/ble_manager.h`
- `platform/nrf52/arduino_common/src/ble/ble_manager.cpp`

承载内容：
- Bluefruit 基础广播/连接入口
- 根据当前协议切换广播服务 UUID
- BLE 名称统一使用 `modules/core_chat` 的共享身份派生规则

当前状态：
- 已建立 nRF52 侧 BLE manager 边界和基础广播能力
- 还需要继续补齐 Meshtastic / MeshCore 的完整手机侧协议服务

---

## 9.5 nRF52 板级 runtime

落点：
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/board_runtime.h`
- `platform/nrf52/arduino_common/src/board_runtime.cpp`

承载内容：
- 3V3 rail 初始化
- 状态 LED / 提示 LED 控制
- GAT562 摇杆与按键原始输入快照
- LoRa 频率字符串格式化

原则：
- 电源 rail / LED / GPIO 输入事实归平台板级 runtime
- LoRa / GPS / UI 不再各自散落初始化相同的板级引脚

---

## 9.6 nRF52 输入 runtime

落点：
- `platform/nrf52/arduino_common/include/platform/nrf52/arduino_common/input_runtime.h`
- `platform/nrf52/arduino_common/src/input_runtime.cpp`

承载内容：
- 摇杆 / 按键的去抖事件
- 最近活动时间戳
- 当前原始输入快照

原则：
- 原始 GPIO -> 可消费输入事件的转换归平台 runtime
- UI 不直接轮询散落 GPIO

---

## 10. GAT562 app 装配层

落点：
- `apps/gat562_mesh_evb_pro/include/apps/gat562_mesh_evb_pro/app_facade_runtime.h`
- `apps/gat562_mesh_evb_pro/src/app_facade_runtime.cpp`
- `apps/gat562_mesh_evb_pro/include/apps/gat562_mesh_evb_pro/app_runtime_access.h`
- `apps/gat562_mesh_evb_pro/src/app_runtime_access.cpp`
- `apps/gat562_mesh_evb_pro/include/apps/gat562_mesh_evb_pro/startup_runtime.h`
- `apps/gat562_mesh_evb_pro/src/startup_runtime.cpp`
- `apps/gat562_mesh_evb_pro/include/apps/gat562_mesh_evb_pro/loop_runtime.h`
- `apps/gat562_mesh_evb_pro/src/loop_runtime.cpp`
- `apps/gat562_mesh_evb_pro/include/apps/gat562_mesh_evb_pro/arduino_entry.h`
- `apps/gat562_mesh_evb_pro/src/arduino_entry.cpp`
- `apps/gat562_mesh_evb_pro/include/apps/gat562_mesh_evb_pro/ui_runtime.h`
- `apps/gat562_mesh_evb_pro/src/ui_runtime.cpp`
- `src/main.cpp`

承载内容：
- GAT562 独立 app facade，不再复用 ESP 偏置的 `AppContext`
- 启动时装配：
  - app config
  - identity bridge
  - node/contact store
  - protocol adapters
  - BLE manager
  - SX1262 radio packet io
  - GPS runtime
- loop 中驱动：
  - GPS tick
  - Mono OLED UI tick
  - 输入事件 -> UI 动作映射
  - raw packet -> active adapter
  - chat core update
  - BLE update
  - event dispatch

原则：
- `apps/` 只负责 assemble，不负责发明共享业务

---

## 11. 与需求文档的对应关系

已经覆盖的主线：
- `User Name / Short Name` -> 本机身份 / BLE 名称 / LoRa 自宣告联动
- GAT562 真实 `no-Team / no-HostLink / no-SD / no-CJK`
- LoRa 参数由 settings 真正驱动 radio 配置
- GNSS / battery / device runtime 不再是假入口
- GAT562 单色 UI 已有独立 shared module 与 nRF52 backend
- 启动日志 / 屏保 / 主菜单 / 会话 / 文本输入 / 身份/无线/设备/GNSS/动作页 已有明确落点
- MeshCore Lite 不再只认 advert，已能把 direct/group app payload 入站成 `AppData`
- Meshtastic Lite 不再只认文本，已能把非文本 `AppData` 入站成 `AppData`

仍需继续补齐的主线：
- Meshtastic / MeshCore 完整 BLE 手机协议服务
- 更完整的消息接收/ACK/控制类业务
- 单色 UI 的编译与实机联调闭环

---

## 12. 分层结论

本轮确定下来的长期结构：
- `modules/`：共享业务、协议、自宣告、身份策略
- `platform/nrf52/`：nRF52 transport / BLE / GNSS / FS / device runtime
- `boards/gat562_mesh_evb_pro/`：板级事实与边界
- `apps/gat562_mesh_evb_pro/`：设备启动与装配
- `variants/gat562_mesh_evb_pro/`：编译环境与能力裁剪

这也是后续继续补齐 GAT562、以及适配更多新硬件时的标准落点。

## Board Ownership Update

- `boards/gat562_mesh_evb_pro/src/gat562_board.cpp` is now the single board owner for GAT562 board-specific hardware coordination.
- Board-owned concerns include: 3V3 power rail, GPIO input mapping, OLED/I2C access, GPS UART bring-up, board default identity, and LoRa radio binding/config application.
- `apps/gat562_mesh_evb_pro/*` remains composition-only: it wires board, shared platform BLE, shared protocol adapters, and shared UI together.
- GAT562 app code must not keep a second board-specific LoRa/GPIO/bus/power owner outside `Gat562Board`.
- Shared BLE logic should stay in `platform/nrf52/arduino_common`, with board-specific prerequisites absorbed by `Gat562Board`.
- BLE ownership rule update: `apps/gat562_mesh_evb_pro` no longer keeps a board-specific `ble_owner` layer. The app composes the shared `platform/nrf52` `BleManager` directly, while board-specific defaults/prerequisites stay in `Gat562Board`.
- GPS startup rule update: app startup should call a single board-facing GPS runtime entry, rather than splitting UART bring-up and config application across multiple app/runtime locations.
- Radio hardware ownership update: GAT562 LoRa rail enable and SPI bus bring-up are board-owned concerns and now live behind `Gat562Board::prepareRadioHardware()`. `sx1262_radio_packet_io` should only perform chip-level RadioLib initialization and packet I/O.
- I2C ownership update: the former standalone `i2c_bus` helper is now folded into `Gat562Board`. Shared OLED/I2C access and the RAII lock live behind `Gat562Board::i2cWire()` and `Gat562Board::I2cGuard`, keeping board bus ownership at a single entry point.
