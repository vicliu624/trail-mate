# Issue #21 T-Deck Meshtastic Fix Checklist

## 问题描述

用户反馈 Trail Mate 在原版 T-Deck 上出现 “Meshtastic 能收不能发，消息最终判定失败” 的现象，而 `.tmp/firmware` 中的参考固件在同一硬件上工作正常。

## 根因归纳

- T-Deck 的 SX1262 初始化没有对齐参考固件，缺少显式的 `TCXO 1.8V`、`DIO2 RF switch` 和 `140mA current limit` 对齐。
- Radio 与 Display 共享 SPI 时，T-Deck 板级代码使用了很短的锁等待时间，可能在 UI flush 期间让 `TX -> RX` 切换失败。
- `MtAdapter` 有多条直接发送路径会绕过 `radioTask()` 的本地 `rx_started` 状态，导致一旦 `startRadioReceive()` 失败，RX 可能被悄悄丢失。
- `radioTask()` 之前只在 `RX_DONE` 后重启接收，对 `CRC_ERR`、`HEADER_ERR`、`TIMEOUT` 这类终止性 IRQ 没有统一恢复逻辑。
- 当前 ACK 逻辑只会等待一次超时然后判失败，没有把已发出的可靠报文保留下来做再次发射，和参考固件的 reliable retransmission 模型存在明显差距。

## 修复清单

- [x] 对齐 T-Deck 的 SX1262 启动参数：显式使用 `TCXO 1.8V`、`DIO2 as RF switch`、`140mA current limit`。
- [x] 把 T-Deck 的关键 Radio SPI 访问改成阻塞锁，避免显示刷新抢占导致的发射后无法及时回到接收态。
- [x] 给 `AppTasks` 增加共享 RX 状态与“请求重启 RX”机制，让直接发送路径和 `radioTask()` 使用同一套接收恢复状态。
- [x] 让 `radioTask()` 在 `RX_DONE / CRC_ERR / HEADER_ERR / TIMEOUT` 后统一执行 RX 重启，而不是只依赖单次局部状态变量。
- [x] 统一 `MtAdapter` 的无线发射入口，所有 Meshtastic 直发路径都通过同一个 `transmitWirePacket()` 完成 TX 后的 RX 恢复。
- [x] 为需要 ACK 的 Meshtastic 发送保留原始 wire packet，并在 ACK 超时后执行最多 `3` 次可靠重发。
- [x] 修正 ACK 超时失败上报时的目标节点、频道与 `channel_hash` 元数据，避免一律误报成 primary channel。
- [x] 在文本发送队列达到最大重试后，补发显式失败结果，避免静默丢弃。

## 范围说明

- 本次修复没有整包移植 `.tmp/firmware` 的 `ReliableRouter` / `NextHopRouter`，但已经把和该问题最直接相关的三层差异补齐：
- 板级射频初始化对齐。
- 发送后回到接收态的恢复链路对齐。
- 可靠发送的 ACK 超时重发能力补齐。

## 建议验证

- `pio run -e tdeck`
- `pio run -e tlora_pager_sx1262`
- `pio run -e lilygo_twatch_s3`
- `pio run -e gat562_mesh_evb_pro`
- `clang-format-14 --dry-run --Werror ...` 按 CI 规则检查格式
