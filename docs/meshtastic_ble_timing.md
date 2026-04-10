# Meshtastic BLE 交互时序

本文整理 `Meshtastic` Android 客户端与当前 `trail-mate` nRF52 固件之间的 BLE 交互时序，目的是为后续排查 “App 一直停在正在连接” 和 `FromRadio/FromNum` 兼容问题提供统一基线。

本文不讨论 UI、GPS、LoRa 或 flash 持久化问题，只聚焦 Meshtastic BLE 握手、配置流和连接完成判定。

## 1. 总体结论

Android 端的 Meshtastic BLE 连接并不是：

- 连接成功
- 收到一两个通知
- 立刻视为已连接

它真正的判定是两阶段配置握手：

1. GATT 连接建立，完成服务发现和通知订阅。
2. App 发出 Stage 1：`ToRadio.want_config_id = CONFIG_NONCE`
3. 设备返回一整段配置流，直到 `config_complete_id(CONFIG_NONCE)`
4. App 发出 Stage 2：`ToRadio.want_config_id = NODE_INFO_NONCE`
5. 设备返回一整段 `node_info` 流，直到 `config_complete_id(NODE_INFO_NONCE)`
6. App 才把连接状态从 `Connecting` 切到 `Connected`

因此，App 长时间停在“正在连接”，通常不表示 BLE 物理链路没有连上，而是表示 Meshtastic 的配置握手尚未被完整认账。

## 2. Android 端时序

### 2.1 连接建立

Android 端 BLE 入口在：

- `.tmp/meshtastic-android/core/network/src/commonMain/kotlin/org/meshtastic/core/network/radio/BleRadioInterface.kt`
- `.tmp/meshtastic-android/core/ble/src/commonMain/kotlin/org/meshtastic/core/ble/KableMeshtasticRadioProfile.kt`

高层时序：

1. `BleRadioInterface.connect()` 建立 GATT 连接
2. `discoverServicesAndSetupCharacteristics()` 完成 service/profile 建立
3. `fromRadio` / `logRadio` 的 observation 被启动
4. 经过一个很短的 `CCCD_SETTLE_MS` 等待窗口
5. 调用 `service.onConnect()`

对应关键代码位置：

- `BleRadioInterface.discoverServicesAndSetupCharacteristics()`
- `BleRadioInterface.service.onConnect()`

### 2.2 App 如何读取 FromRadio

Android 端并不是单纯依赖 `FROMNUM` 通知。

`KableMeshtasticRadioProfile.fromRadio` 的行为如下：

1. 如果设备支持 `FROMRADIOSYNC`，则直接订阅该特征。
2. 如果不支持，则退回到 legacy 模式：
   - 订阅 `FROMNUM`
   - 但是同时主动触发一次 drain
   - 然后循环读取 `FROMRADIO`
   - 一直读到返回空包为止

更具体地说，legacy 模式里会发生两件关键动作：

1. `triggerDrain.tryEmit(Unit)` 会在 collector 启动时主动触发一次。
2. `sendToRadio()` 每发送一条 `ToRadio`，也会再次 `triggerDrain.tryEmit(Unit)`。

这意味着：

- 发送 `want_config_id` 后，App 会主动开始 `read(FROMRADIO)`
- 即使某一瞬间没有 `FROMNUM` 通知，App 也不一定会停住
- `FROMNUM` 更像 steady-state 提示，而不是唯一驱动条件

对应关键代码位置：

- `KableMeshtasticRadioProfile.fromRadio`
- `service.observe(fromNum)`
- `service.read(fromRadioChar)`
- `triggerDrain.tryEmit(Unit)`
- `sendToRadio()`

### 2.3 两阶段握手

上层状态机在：

- `.tmp/meshtastic-android/core/data/src/commonMain/kotlin/org/meshtastic/core/data/manager/MeshConnectionManagerImpl.kt`
- `.tmp/meshtastic-android/core/data/src/commonMain/kotlin/org/meshtastic/core/data/manager/MeshConfigFlowManagerImpl.kt`
- `.tmp/meshtastic-android/core/data/src/commonMain/kotlin/org/meshtastic/core/data/manager/FromRadioPacketHandlerImpl.kt`

#### Stage 1

1. 连接建立后，`MeshConnectionManagerImpl.handleConnected()` 调用 `startConfigOnly()`
2. `startConfigOnly()` 发送：
   - `ToRadio.want_config_id = HandshakeConstants.CONFIG_NONCE`
3. App 开始消费 `FromRadio`
4. `FromRadioPacketHandlerImpl` 将不同 variant 分发到 config flow manager / config handler
5. 当收到：
   - `FromRadio.config_complete_id == CONFIG_NONCE`
6. `MeshConfigFlowManagerImpl.handleConfigComplete()` 进入 Stage 1 complete

Stage 1 期间典型接收内容包括：

- `my_info`
- `deviceui`
- `metadata`
- `config`
- `moduleConfig`
- `channel`
- `fileInfo`

#### Stage 2

Stage 1 完成后：

1. `MeshConfigFlowManagerImpl.handleConfigOnlyComplete()` 先发送一个 heartbeat
2. 然后调用 `startNodeInfoOnly()`
3. 发送：
   - `ToRadio.want_config_id = HandshakeConstants.NODE_INFO_NONCE`
4. App 接收一串 `node_info`
5. 当收到：
   - `FromRadio.config_complete_id == NODE_INFO_NONCE`
6. `MeshConfigFlowManagerImpl.handleNodeInfoComplete()` 才真正执行：
   - `serviceRepository.setConnectionState(ConnectionState.Connected)`

也就是说，只有 Stage 2 完成，Android 才认为连接完成。

## 3. 当前固件侧时序

当前 nRF52 侧入口主要在：

- `platform/nrf52/arduino_common/src/ble/meshtastic_ble.cpp`
- `modules/core_chat/src/ble/meshtastic_phone_core.cpp`

### 3.1 BLE service 层

当前 Meshtastic BLE service 暴露的关键特征：

- `ToRadio`
- `FromRadio`
- `FromNum`
- `LogRadio`

主循环的关键顺序目前是：

1. `processPendingToRadio()`
2. `handleToPhone()`
3. `prepareReadableFromRadio()`

也就是：

- 先处理手机写入的 `ToRadio`
- 再让 `MeshtasticPhoneCore` 产出下一帧 `FromRadio`
- 再把下一帧预装进 `FromRadio` characteristic，等待 App 读取

### 3.2 PhoneCore 配置流

`MeshtasticPhoneCore` 在收到 `want_config_id` 后会开始吐配置快照。

当前日志里能看到的配置流顺序大致是：

1. `cfg#N start`
2. `frame my_info`
3. `frame deviceui`
4. `frame self_node`
5. 后续 metadata/config/module/channel/node 等若干帧
6. `cfg#N complete`

对应日志前缀：

- `[BLE][mtcore][cfg#N] start`
- `[BLE][mtcore][cfg#N] frame ...`
- `[BLE][mtcore][cfg#N] complete`

每帧编码后都会成为一条 `MeshtasticBleFrame`，交给 transport 层。

### 3.3 FromNum / FromRadio 当前实现

当前 nRF52 transport 的基本模型是：

1. `PhoneCore.popToPhone()` 产出一帧
2. `handleToPhone()` 将其缓存为待发送帧
3. `prepareReadableFromRadio()` 把这帧写入 `FromRadio`
4. `notifyFromNum(from_num)` 发出一条 `FromNum` 通知
5. App 如果读取 `FromRadio`，则走 `onFromRadioAuthorize()`
6. 读成功后 `markReadableFromRadioConsumed()` 清空当前可读帧

为了排障，目前固件还打印了这些日志：

- `[BLE][nrf52][mt][flow] link-up ...`
- `[BLE][nrf52][mt][flow] from_num subscribed=...`
- `[BLE][nrf52][mt][flow] preload from_num=... len=...`
- `[BLE][nrf52][mt][flow] from_num notify=...`
- `[BLE][nrf52][mt] from_radio read len=...`
- `[BLE][nrf52][mt] from_radio read empty`

## 4. 双方时序的核心关系

把 Android 和固件合在一起，可以归纳成下面这条主线：

1. GATT connected
2. App 订阅 `FROMNUM` / `LOGRADIO`，并建立 `fromRadio` collector
3. App 调用 `service.onConnect()`
4. App 发送 `want_config_id = CONFIG_NONCE`
5. App 主动开始 drain `FROMRADIO`
6. 固件一帧一帧提供：
   - `my_info`
   - `deviceui`
   - `self_node`
   - ...
   - `config_complete(CONFIG_NONCE)`
7. App 切到 Stage 2，发送 `want_config_id = NODE_INFO_NONCE`
8. App 再次 drain `FROMRADIO`
9. 固件提供若干 `node_info`
10. 固件发送 `config_complete(NODE_INFO_NONCE)`
11. App 切为 `Connected`

## 5. 当前 nRF52 侧最值得关注的偏离点

基于 Android 端真实代码，当前最重要的观察点不是“有没有大量 `FROMNUM notify`”，而是以下几条。

### 5.1 App 是否真的在读 FromRadio

由于 Android 在发送 `want_config` 后会主动 drain `FROMRADIO`，所以如果固件日志里看不到：

- `[BLE][nrf52][mt] from_radio read len=...`
- `[BLE][nrf52][mt] from_radio read empty`

那问题更像是：

- `FromRadio` 的 GATT read 在 nRF52/Bluefruit 上没有真正对上 App 的读取
- 而不是配置内容本身不对

### 5.2 空包语义是否闭环

Android 端 legacy 模式会一直 `read(FROMRADIO)`，直到返回空包才结束本轮 drain。

因此固件必须保证：

1. 有帧时，read 返回当前帧
2. 当前帧被读走后，下一次 read 应该拿到下一帧
3. 本轮没有更多帧时，必须返回空包

如果最后一步没有成立，App 可能一直认为配置流没有完整结束。

### 5.3 Stage 1 完成不等于连接完成

即使 `cfg#1 complete` 已经在固件日志里出现，App 也仍可能显示“正在连接”。

因为对 Android 来说：

- Stage 1 complete 只是配置读取完成
- 还需要再跑一次 Stage 2 node-info 握手
- 只有第二个 `config_complete_id` 到达，状态才会变成 `Connected`

因此任何只完成 Stage 1 的链路，都会让 UI 继续停留在 `Connecting`

### 5.4 历史问题：loop 栈溢出

之前 nRF52 侧已经确认过一个与 BLE 配置流强相关的历史问题：

- 配置流构造路径曾把 `loop` 任务栈压到 `stack_hwm=0`
- 这会导致同任务中的其他对象被踩坏
- 表现为 GPS 状态损坏、`SAT` 数异常、guard 被 `[BLE` 字样改写

该问题经过 `MeshtasticPhoneCore` 的大对象减栈后已经明显缓解，但它说明：

- Meshtastic 配置流不是“普通小开销路径”
- 任何时序分析都需要连同任务上下文和内存行为一起看

## 6. 用这份时序来判定故障

后续排障可以按下面的判定法来做。

### 情况 A

现象：

- 有 `cfg#start`
- 有 `cfg#complete`
- 但没有任何 `from_radio read ...`

判断：

- App 已经发出 `want_config`
- 但 `FROMRADIO` 读路径没有真正命中 nRF52 固件
- 应重点排查 `FromRadio` characteristic 的 GATT read 兼容性

### 情况 B

现象：

- 有 `from_radio read len=...`
- 但没有 `from_radio read empty`

判断：

- drain-until-empty 没闭环
- App 很可能还在等待本轮读取结束

### 情况 C

现象：

- Stage 1 的 `config_complete(CONFIG_NONCE)` 已发送
- 但 App 没进入第二次 `want_config_id`

判断：

- App 没有成功消费到 Stage 1 完成信号
- 应优先核对 `config_complete_id` 帧是否真的到达 Android `FromRadioPacketHandler`

### 情况 D

现象：

- Stage 2 也已经完成
- 但 App 还是 `Connecting`

判断：

- 应核对 Android 侧 `MeshConfigFlowManagerImpl.handleNodeInfoComplete()` 是否真的被触发
- 或排查 Stage 2 期间是否存在 node-info 流中断 / 状态机被回退

## 7. 后续建议

后续所有 Meshtastic BLE 修复，都应优先对照本文中的这几条事实：

1. Android 会主动 drain `FROMRADIO`
2. `FROMNUM` 不是唯一驱动条件
3. 连接完成依赖两阶段 `config_complete_id`
4. `FROMRADIO` 必须具备“连续读帧直到空包”的稳定语义
5. nRF52 上不仅要关注协议顺序，还要关注任务栈和回调上下文

如果后续继续调试，建议优先保留以下日志：

- `[BLE][nrf52][mt][flow] link-up ...`
- `[BLE][nrf52][mt][flow] from_num subscribed=...`
- `[BLE][nrf52][mt][flow] preload ...`
- `[BLE][nrf52][mt][flow] from_num notify=...`
- `[BLE][nrf52][mt] from_radio read len=...`
- `[BLE][nrf52][mt] from_radio read empty`
- `[BLE][mtcore][cfg#N] start/frame/complete`
- `[BLE][mtcore][rt] stage=... stack_hwm=...`

这些日志已经足够把问题收敛到：

- GATT 读不到
- drain 语义不闭环
- Stage 1 未完成
- Stage 2 未完成
- 或运行时栈/内存问题
