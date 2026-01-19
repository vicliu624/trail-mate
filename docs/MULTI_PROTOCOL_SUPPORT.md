# 多协议支持实现说明

本文档说明当前 Trail Mate 在 "Meshtastic + MeshCore" 双协议场景下的实现方式。
**协议不会在运行中自动判定或切换**，而是通过设置明确选择其中一个协议运行。

---

## 1) 协议选择方式

当前协议由设置项确定：
- `AppConfig::mesh_protocol`
- 持久化键：`mesh_protocol`
- 取值：
  - `Meshtastic`
  - `MeshCore`

系统启动时读取配置，并通过 `ProtocolFactory` 创建对应的 adapter。运行期间不会动态切换。

---

## 2) 代码结构（单协议运行）

核心思路：
- **UI 层只与 `IMeshAdapter` 交互，不感知协议**
- 只创建一个 adapter（Meshtastic 或 MeshCore）
- Radio 任务的原始数据直接交给选定 adapter 处理

### 结构 ASCII 图

```
+------------------+
|   UI / UseCase   |
+------------------+
          |
          v
+-------------------------+
|     IMeshAdapter        |
+-------------------------+
          |
          v
+------------------+
| Meshtastic OR    |
| MeshCore Adapter |
+------------------+
```

---

## 3) 接收流程（无动态判定）

1. Radio 任务收到原始包
2. 直接调用当前 adapter 的 `handleRawPacket()`
3. 解析出的文本消息进入 `ChatService`

> 不再进行协议预判，也不维护节点协议映射。

---

## 当前实现状态说明

- Meshtastic：功能完整（含 NodeInfo 解析）
- MeshCore：目前只实现了 RAW_CUSTOM 文本收发（最小闭环）
- TXT_MSG / 加密 / 认证：待实现

---

## 扩展建议

如需支持更多协议：
- 在 Settings 中新增协议选项
- 新建对应 adapter
- 仍保持“运行时单协议”的策略，避免混跑与误判
