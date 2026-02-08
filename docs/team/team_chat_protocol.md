﻿﻿# Team Chat 协议方案（方案 C，v0.1）

本文件描述“队伍聊天”协议的最小可实施方案，用于承载结构化消息：
Text / Location / Command，并与 Team 安全域绑定，支持地图与态势联动。

---

## 1. 目标与范围

- 目标：让队伍聊天成为“可解析、可执行”的结构化消息流。
- 覆盖媒体类型：
  - Text：普通文本。
  - Location：位置分享，可在 Chat/GPS 地图渲染。
  - Command：指令类消息，可触发队伍行动提示与地图标注。
- 安全：仅队伍成员可解密，不依赖 Meshtastic 普通聊天频道。

前置条件（v0.2 调整）：
- **建队/入队不再通过 LoRa 或 NFC**。
- 组队只在 5 米内的近距离场景发生，**使用 ESP‑NOW 完成建队与密钥分发**。
- LoRa 仅用于队内日常通信（Team Chat/Position/Track），不参与组队流程。

非目标：
- 不替换现有的普通聊天/广播消息。
- 不实现复杂可靠传输（v0.1 先做 best-effort）。

---

## 2. 端口与加密

- 新增端口：
  - `TEAM_CHAT_APP = 303`（在 `src/team/protocol/team_portnum.h` 中定义）
- 加密方式：
  - 复用 `TeamEncrypted` envelope（`team_wire.h`）。
  - 从 `team_psk` 派生 `team_chat` key。
  - 建议在 `TeamService::setKeysFromPsk()` 中新增：
    - `deriveKey(psk, "team_chat", keys.chat_key)`

这样可实现“队伍聊天协议层隔离”，非队伍成员无法解密。

---

## 3. Payload 结构（v0.1）

### 3.1 通用头部

使用简洁 TLV 或固定头 + 变长 payload。建议固定头：

```
struct TeamChatHeader {
  uint8_t  version;    // =1
  uint8_t  type;       // 1=Text 2=Location 3=Command
  uint16_t flags;      // 预留
  uint32_t msg_id;     // 本地生成，防重放/去重
  uint32_t ts;         // 发送时间（unix）
  uint32_t from;       // 发送者 node_id
};
```

### 3.2 Text

```
struct TeamChatText {
  // UTF-8 text bytes
  bytes text;
};
```

### 3.3 Location

```
struct TeamChatLocation {
  int32_t lat_e7;
  int32_t lon_e7;
  int16_t alt_m;       // optional, 0=unknown
  uint16_t acc_m;      // optional, 0=unknown
  uint32_t ts;         // optional, 0=use header.ts
  uint8_t  source;     // 0=GPS 1=Manual 2=CommandTarget
  bytes    label;      // optional short label
};
```

### 3.4 Command

v0.1 仅做最小指令集，不做撤销/过期语义。

```
enum TeamCommandType : uint8_t {
  RallyTo = 1,   // 集结到目标点
  MoveTo  = 2,   // 前往目标点
  Hold    = 3    // 原地待命
};

struct TeamChatCommand {
  uint8_t  cmd_type;
  int32_t  lat_e7;      // 可选：用于 Rally/Move
  int32_t  lon_e7;
  uint16_t radius_m;    // 可选：集合半径
  uint8_t  priority;    // 0=normal 1=high
  bytes    note;        // 可选：简短备注
};
```

---

## 4. 发送与接收流程

### 4.1 发送

- UI 产生 `Text/Location/Command`。
- 编码为 `TeamChatHeader + payload`。
- 通过 `TeamService::sendTeamChat()`：
  - 使用 `keys.chat_key` 加密封装为 `TeamEncrypted`。
  - 通过 `TEAM_CHAT_APP` 发送（`mesh_.sendAppData(...)`）。

### 4.2 接收

- `TeamService::processIncoming()` 新增 `TEAM_CHAT_APP` 分支：
  - 解密 `TeamEncrypted`。
  - 解析 `TeamChatHeader`。
  - 触发 `TeamChatEvent`（新增 EventBus 类型）。
- UI 接收到 `TeamChatEvent`：
  - 追加到 `team_ui_chatlog`（新增类型字段）。
  - 若是 Location/Command，同步更新地图/GPS 页标注。

---

## 5. 与现有代码的改动点

最小改动清单（v0.1）：

1. **协议与端口**
   - `src/team/protocol/team_portnum.h` 新增 `TEAM_CHAT_APP = 303`
   - 新增 `team_chat.h/.cpp`（编码/解码）

2. **密钥派生**
   - `TeamKeys` 增加 `chat_key`
   - `TeamService::setKeysFromPsk()` 中派生 `"team_chat"`

3. **TeamService**
   - 新增 `sendTeamChat(...)`
   - `processIncoming()` 处理 `TEAM_CHAT_APP`

4. **事件总线**
   - `sys/event_bus.h` 新增 `TeamChatEvent`

5. **UI / 存储**
   - `team_ui_chatlog_append()` 扩展为结构化消息（type + payload）
   - `Contacts/Team` 聊天页改用 TeamChat 数据源渲染卡片

---

## 6. 兼容性与过渡

- 保留现有普通聊天：
  - Primary/Secondary 继续用 TEXT_MESSAGE_APP
- Team Chat 作为独立通道：
  - 不影响普通聊天历史与通知
  - 可以逐步替换 Team 页的会话来源

---

## 7. v0.1 已确定事项
- ACK/送达：不需要（尽力而为）。
- Command 撤销/过期：不支持。
- 地图交互：收到消息只弹系统通知；弹窗由 Chat 会话中选中地图标注后触发，显示该位置瓦片地图的裁剪图。
- 组队模式 GPS：从 posring.log 渲染队员位置。

## 8. 版本策略

- `TeamChatHeader.version` = 1
- 后续扩展通过 `flags` 或新 `type` 兼容

---

# 附录 A：ESP‑NOW 建队与入队（v0.2）

> 目标：减少步骤与不稳定环节，近距离（≤5m）快速建队。

## A1. 角色与约束
- 场景：所有人同一房间/范围 ≤5m。
- 介质：ESP‑NOW（2.4GHz），不再使用 LoRa/NFC。
- 结果：成员收到 **team_psk + team_id + key_id + epoch**，之后用 LoRa 收发 Team 协议。

## A2. 简化流程

**Leader**
1) Create Team → 打开 “Pairing (ESP‑NOW)” 窗口（默认 120s）。
2) 接收成员 Join 请求（可“Auto‑accept”或手动 Accept）。
3) 发送 KeyDist（ESP‑NOW）给新成员。

**Member**
1) Join Team → Nearby Teams（ESP‑NOW 扫描）选择目标。
2) 发送 Join 请求并等待 KeyDist。
3) 收到 KeyDist 后进入已加入状态。

## A3. ESP‑NOW 消息（建议）

```
PAIR_BEACON { team_id_short, team_id, epoch, key_id, leader_id, expires_at }
PAIR_JOIN   { team_id, member_id, member_pub, nonce }
PAIR_ACCEPT { team_id, member_id, ok, nonce }
PAIR_KEY    { team_id, key_id, epoch, team_psk_encrypted, nonce }
```

说明：
- `team_psk_encrypted` 可用临时会话密钥（由 `member_pub` + leader 私钥派生）加密。
- `nonce` 用于关联请求与防重放。
- 所有 ESP‑NOW 包都只在 Pairing 窗口内处理。

## A4. 与 Team Chat 的关系
- ESP‑NOW 仅负责“建队/入队/密钥分发”。
- 完成后使用 LoRa 的 Team 协议（TEAM_CHAT / TEAM_POS / TEAM_TRACK / …）。
