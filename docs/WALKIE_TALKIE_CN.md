# 对讲机实现（FSK + Codec2）

本文档描述 T-LoRa-Pager（SX1262）的对讲机实现。
涵盖信号流程、协议封装、缓冲与调参要点。

## 范围与目标
- 设备：LilyGo T-LoRa-Pager（SX1262 + ES8311）。
- 模式：LoRa 无线的 FSK 语音，半双工（TX 或 RX）。
- 目标延时：RF 良好时 ~200ms，RF 较差时自适应到 ~300ms。
- 重点：稳定性与可懂度优先，不追求极限低延时。

## 文件位置
- 核心服务：`src/walkie/walkie_service.cpp`
- UI：`src/ui/ui_walkie_talkie.cpp`
- 输入钩子：`src/ui/LV_Helper_v9.cpp`

## 无线配置（FSK）
进入 walkie 模式时，电台从 LoRa 切换到 FSK。

- 码率：9.6 kbps
- 频偏：5.0 kHz
- RX 带宽：156.2 kHz
- 前导码：16
- 同步字：0x2D 0x01
- CRC：2 字节

这些参数在 `walkie_service.cpp` 的 `configure_fsk()` 中设置。

## 音频配置
- Codec：ES8311（I2S）
- I2S 格式：16-bit，2 通道
- 采样率：8000 Hz
- Codec2 模式：3200 bps

I2S 以立体声读取，混合为单声道供 Codec2 编码，播放时再把单声道复制为双声道。

## 数据包封装
每个 RF 包含 5 帧 Codec2（约 100 ms 音频）。

Header（14 字节）：
- 0: 'W'
- 1: 'T'
- 2: version (2)
- 3: type (0x01 表示语音)
- 4..7: self_id (u32, LE)
- 8..9: session_id (u16, LE)
- 10..11: seq (u16, LE)
- 12..13: frame0_index (u16, LE)

Payload：
- 5 帧 Codec2，每帧 `bytes_per_frame` 字节。

### 空口预算说明
在 9.6 kbps FSK 下：
- Codec2 3200 bps，20 ms/帧，8 字节/帧（典型）→ 5 帧 ≈ 40 字节
- Header ≈ 9 字节 → 每 100 ms 约 50 字节
- 50 字节 ≈ 400 bit / 100 ms ≈ 4 kbps（仅 payload + header）

在干净的 RF 环境中可满足 9.6 kbps，但前导码、同步字、CRC、
状态切换和丢包会降低有效吞吐。
因此短距离可能很顺，噪声环境会明显变卡。

## TX 流水线（稳定 + 缓冲）
1) 采集 I2S 音频（立体声）。
2) L/R 混合为单声道，应用 TX 增益（`kTxPcmGain`）。
3) Codec2 编码（3200）输出帧。
4) 推入 TX 帧队列（最大 20 帧）。
5) TX 空闲且队列 >=5 帧时：
   - 组包 5 帧。
   - `startTransmit()` 发送。
   - 轮询 `TX_DONE` 再发下一包。

为何这样设计：
- 采样与无线发送解耦。
- 发送不再阻塞音频采集。
- 包速稳定，时序可预测。

### TX 时序（ASCII）
```
20ms frames (Codec2) ----> group 5 frames ----> 1 RF packet (~100ms audio)

Mic/I2S  : [F1][F2][F3][F4][F5][F6][F7][F8][F9][F10]...
Codec2   :  C1  C2  C3  C4  C5  C6  C7  C8  C9  C10
TX queue :  C1  C2  C3  C4  C5 | C6  C7  C8  C9  C10 | ...
RF TX    :       [PKT1]              [PKT2]          ...
```

## RX 流水线（抖动缓冲 + 自适应预缓冲）
1) Radio RX ISR/poll 读包并校验 header 与 self_id。
2) 帧写入 RX 抖动缓冲（最大 25 帧）。
3) 播放以固定 20ms 节拍运行：
   - 预缓冲达到阈值后启动播放。
   - 每 20ms 取一帧解码播放。
   - 欠载时重复上一帧（简单 PLC）。
   - 长时间无音则暂停并输出静音。

自适应预缓冲：
- RF 好：预缓冲 = 10 帧（~200ms）
- RF 差：预缓冲 = 15 帧（~300ms）
- 1 秒内 underrun > 1 → 进入 300ms
- 连续 3 秒无 underrun → 回到 200ms

关键参数：
- `kJitterMinPrebufferFrames` = 10
- `kJitterMaxPrebufferFrames` = 15
- `kJitterMaxFrames` = 25
- `kTxQueueMaxFrames`（默认 20，~400ms）
- `kCodecFramesPerPacket`（5 帧，~100ms）

### RX 时序（ASCII）
```
RF packets (100ms audio each)

RF in     :    [PKT1]   [PKT2]   [PKT3]   [PKT4] ...
Frame FIFO:   F1..F5 | F6..F10 | F11..F15 | ...
Playback :         [F1][F2][F3][F4][F5][F6][F7]...
Prebuffer:       ^ start after N frames (N=10..15)
```

RF 不稳定会出现 underrun：
```
Frame FIFO:  F1..F5 | (gap) | F6..F10
Playback :  [F1][F2][F3][F4][F5][F5][F5]... (repeat last frame)
```

## 如何提升音质（全链路）
这是用于提高清晰度与稳定性的端到端策略。

### 1) 稳定时序（TX + RX）
- TX 采用帧队列，编码不被无线发送阻塞。
- RX 采用抖动缓冲，以固定 20ms 节拍播放。
- 解决播放抖动和“断续”。

### 2) 自适应缓冲深度
- RF 好：200ms 预缓冲（低延时）。
- RF 差：300ms 预缓冲（更稳定）。
- 根据 underrun 自动切换。

### 3) 固定封包节奏
- 每个 RF 包固定 5 帧 Codec2（100ms 音频）。
- RX 缓冲更可预测。

### 4) PLC（丢包隐藏）
- 帧丢失时重复上一帧。
- 避免明显“断音”和爆裂。

可选提升（低成本）：
- 短丢包（<=2 帧）：对重复帧做线性衰减。
- 长丢包：淡出到静音或舒适噪声。

### 5) 增益分配（TX + RX）
- TX 麦克风增益（`kDefaultGainDb`）保证可懂度。
- TX PCM 增益（`kTxPcmGain`）避免电平过低。
- RX PCM 增益（`kRxPcmGain`）提高可听响度。

### 6) 半双工安全
- TX 与 RX 互斥，避免自干扰。
- 平滑切换减少抖动。

可选提升：
- TX 尾音 / hang time（100–200 ms），发完队列再回 RX。
- RX squelch，忽略切换后极短噪声。

### 7) 实用调参路径
若音频断续 → 增加预缓冲或缓冲深度。
若声音偏小 → 提高 mic 增益或 RX PCM 增益。
若失真 → 降低增益或启用限幅。

## 端到端时序图（ASCII）
```
PTT press
  |
  v
Mic capture (20ms) -> Codec2 encode -> TX queue -> RF TX (100ms pkt)
                                                       |
                                                       v
                                                RF RX -> Jitter buffer
                                                       |
                                                       v
                                          Decode (20ms cadence) -> DAC out
```

## 增益与音量
这些参数用于提高清晰度与稳定性：
- Mic gain: `kDefaultGainDb` (36 dB)
- TX PCM gain: `kTxPcmGain` (1.6x)
- RX PCM gain: `kRxPcmGain` (2.0x)
- 默认音量：80

在 walkie 模式中，旋钮每刻度调整 1 级音量。

## 半双工行为
- TX 与 RX 互斥。
- PTT 按下：进入 TX，RX 停止。
- 松开：回到 RX。

## 调参指南
若音频断续：
- 增加 `kJitterMaxPrebufferFrames` 到 20（~400ms）。
- 频繁溢出则增大 `kJitterMaxFrames`。

若延时太高：
- 减少 `kJitterMinPrebufferFrames` 到 8（~160ms），
  但 RF 差时会更容易 underrun。

若音量偏小：
- 提高 `kDefaultGainDb`（mic 增益）。
- 提高 `kRxPcmGain`（播放增益）。

若失真：
- 降低 `kTxPcmGain` 或 `kRxPcmGain`。
- 降低 `kDefaultGainDb`。

## 已知限制
- 非全双工：无法同时 TX/RX。
- RF 表现依赖 CN470 规则及现场环境。
- Codec2 3200 bps 以可懂度优先于高保真。

## 设计评审笔记（进一步改进）
这些结构性改进用于减少断续并提升清晰度。

### 1) 序列与会话鲁棒性
当前 header 已实现：
- `session_id`（u16）：每次 PTT 作为新 talkspurt。
- `seq`（u16）：包序号。
- `frame0_index`（u16）：该包第一个 20ms 帧的索引。

好处：
- 丢弃回绕后旧包。
- 区分新旧 talkspurt。
- PLC 能准确知道缺失帧数。

### 2) 自适应预缓冲滞回
当前逻辑较简单，避免震荡可：
- 统计滑动窗口（如 5s）内的丢包/欠载率。
- 设定滞回阈值：
  - 丢包率 > X% → 深缓冲
  - 丢包率 < Y% → 浅缓冲（Y < X）

### 3) RX 带宽调参
RX BW = 156.2 kHz（鲁棒但噪声大）。
建议可配置：
- 窄带（58/83 kHz）在干净环境下提升 SNR。
- 宽带（156 kHz）在频偏/时钟漂移大时更稳。

### 4) 可观测性（关键）
建议在日志或 UI 暴露：
- RX 抖动缓冲占用（当前/最大）
- Underrun 计数（1s / 5s）
- Overflow/丢帧计数
- RSSI/SNR（若 FSK 可用）
- 基于 seq/frame index 的丢包估计
