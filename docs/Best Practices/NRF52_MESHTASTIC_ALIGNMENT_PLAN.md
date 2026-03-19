# GAT562 / nRF52 对齐 `.tmp/meshtastic-firmware` 改造清单

## 基准原则

- 以 `.tmp/meshtastic-firmware` 的平台分层方式为准，不自创新架构
- 共享协议核心负责业务语义
- 平台蓝牙宿主只负责 transport / callback / notify
- 板级 owner 只负责硬件事实：LoRa / GPS / I2C / 输入 / 时间源

## 参考实现映射

- 参考共享核心
  - `.tmp/meshtastic-firmware/src/mesh/PhoneAPI.cpp`
  - `.tmp/meshtastic-firmware/src/mesh/MeshService.cpp`
- 参考平台宿主
  - `.tmp/meshtastic-firmware/src/platform/nrf52/NRF52Bluetooth.cpp`
  - `.tmp/meshtastic-firmware/src/nimble/NimbleBluetooth.cpp`

## 当前偏差

- `platform/nrf52/arduino_common/src/ble/meshtastic_ble.cpp`
  - 仍承担过多 Meshtastic phone 协议与配置响应逻辑
- `platform/nrf52/arduino_common/src/ble/meshcore_ble.cpp`
  - 仍承担过多 MeshCore 命令解释与设备状态拼装逻辑
- `platform/nrf52/arduino_common/src/chat/infra/meshtastic/meshtastic_radio_adapter.cpp`
  - 同时承担 radio codec、部分业务拼包、节点宣告
- `platform/nrf52/arduino_common/src/chat/infra/meshcore/meshcore_radio_adapter.cpp`
  - 同时承担 radio codec 与 MeshCore 业务桥接
- nRF BLE 文件此前直接依赖 `gat562_board`
  - 违反“board owner 提供事实，上层只消费”

## 改造顺序

### 1. 先把 BLE host 变薄

- BLE 层不再直接依赖 `boards/gat562_mesh_evb_pro`
- BLE 层通过 `app::IAppBleFacade` 消费：
  - 当前时间同步入口
  - node store
  - chat service
  - mesh adapter

### 2. 抽 Meshtastic 共享 phone core

- 从 `platform/nrf52/arduino_common/src/ble/meshtastic_ble.cpp` 下沉：
  - `handleToRadio`
  - `handleToRadioPacket`
  - admin/config/module config 响应
  - `FromRadio` 编码与队列状态生成
- 目标形态：
  - 平台无关 `MeshtasticPhoneCore`
  - nRF BLE 仅保留 characteristic / advertising / read-write callback

### 3. 抽 MeshCore 共享 phone core

- 从 `platform/nrf52/arduino_common/src/ble/meshcore_ble.cpp` 下沉：
  - 命令分发
  - contact/status/device info 帧拼装
  - raw data push / telemetry push 规则
- 目标形态：
  - 平台无关 `MeshCorePhoneCore`
  - nRF BLE 仅保留 NUS 风格收发宿主

### 4. 收拢 radio 入口

- 保持 `boards/gat562_mesh_evb_pro/src/sx1262_radio_packet_io.cpp` 为唯一板级 radio owner
- Meshtastic / MeshCore 共享 `IRadioPacketIo`
- adapter/core 不再碰板级 SPI / pin / IRQ

### 5. 收拢 GPS / 时间 owner

- GAT562 板级继续提供：
  - GPS 上电/串口/NMEA 解析
  - 当前 epoch 事实
  - time synced 状态
- 上层仅通过 facade / runtime 接口消费，不直连 board 类

### 6. 再处理命名与残留层

- 当 `*_lite` 不再准确时再重命名
- 不保留兼容层
- 不新增临时 runtime

## 本轮已开始执行

- 已新增 `IAppBleFacade::syncCurrentEpochSeconds()`
- 已让 nRF BLE 的 Meshtastic / MeshCore 时间同步改走 facade，而不是直接触达 `gat562_board`

## 下一批代码动作

- 把 Meshtastic BLE 中的 phone 协议逻辑抽成独立 core
- 让 `MeshtasticBleService` 只剩 Bluefruit host 职责
- 再对 MeshCore 做同样处理
