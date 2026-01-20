# 你这个 TEAM 业务到底在做什么（我现在能总结到“可写 TeamEvent”的程度）

## 1) Team 的定义（产品级一句话）

**Team = 临时形成、目标驱动、随时解散的户外行动单元。**

它不是“群聊”，也不是“普通位置共享”，而是让一小群人在无网络环境下持续回答三件事：

1. **我们是否还在一起**（队伍是否分散/掉队/走偏）
2. **接下来要做什么**（集合、等待、求助等“行动共识”）
3. **有没有人出问题**（静止、失联、电量低、SOS）

---

## 2) Team 的核心能力（你已经确定的）

### A. 临时组队（Create / Join / Disband）

* Leader 一键建队
* 成员通过短码/QR 拿到 **TeamKey** 加入
* Team 可以随时解散（解散即密钥销毁）

**这一套你已经做成状态机：Create → Active → Rotate → Disband**

---

### B. 强隐私位置共享（你的差异化关键）

你明确在意：

> **位置广播出去后，只有 Team 成员能解析成功；即便 Team channel 配置外传，也看不懂。**

因此你采用两层保护：

* L1：Meshtastic **Channel** 加密（CH-ENC）
* L2：应用层 **E2EE**（TeamKey 派生的 PosKey/WpKey，TeamEncrypted Envelope）

并且你倾向新增自己的 PortNum：

* `TEAM_POSITION_APP`
* `TEAM_WAYPOINT_APP`
* 管理类 `TEAM_MGMT_APP`

---

### C. Team 内的“行动指令”（Preset / Command）

你提出的差异化：

* 向队友发 **预设指令**（集合、等待、求助…）
* 指令携带必要参数（例如集合地点 waypoint）
* 指令是“事件”，不靠打字聊天

> 注意：你前面没要求我把它做成完整协议，但你已经明确这是 Team 的重要业务能力。

---

### D. Team 生命周期复盘（你现在要做的）

你希望**整个组队过程可复盘**，包括生命周期中所有事件落地。

所以我们引入：

* SD 日志（append-only）
* LogRecord（AEAD + CRC）
* snapshot 加速（可选）
* LogKey（独立于 TeamKey，可配置是否保留）

---

## 3) Team 生命周期里“会发生哪些事件”（这直接决定 TeamEvent）

为了写 `TeamEvent` 的 protobuf，你需要先承认：
**TeamEvent 不等于“所有无线包”**，而是**业务层发生的事情**，能用于复盘与解释行为。

你当前业务可以归纳为 5 类事件：

### 1) 生命周期事件（Lifecycle）

* Team 被创建（Leader 本地）
* 广播可发现（ADVERTISE）
* 有人请求加入（JOIN_REQUEST）
* Leader 接受并下发配置（JOIN_ACCEPT）
* 成员确认入队（JOIN_CONFIRM）
* Team 状态同步（TEAM_STATUS）
* 密钥轮换（KEY_ROTATE）
* 解散/离开（DISBAND / LEAVE）

这些事件用于回答：

* “队伍什么时候开始/结束”
* “谁在队里、什么时候加入/离开”
* “什么时候换过密钥、为什么换”

---

### 2) 位置与轨迹事件（Telemetry）

* 收到某成员位置（POSITION_RX）
* 收到航点/集合点（WAYPOINT_RX）

这些事件用于复盘：

* 人在什么时候到过哪里
* 队伍是否分散、何时重新聚拢

> 位置事件会有降采样策略，否则日志爆炸。

---

### 3) 指令事件（Command）

* 集合指令（含地点 waypoint）
* 等待/暂停
* 求助（SOS，含位置/严重级别）

这些事件用于复盘：

* “谁下达过行动共识”
* “队伍如何响应”

---

### 4) 连接与可见性事件（Presence/Health）

这类你在产品目标里明确需要，但我们之前没有把它细化成协议字段，现在总结出来：

* 某成员“暂时不可见”（超时没收到位置）
* 恢复可见
* 电量低（如果你决定上报）
* 静止过久（如果你决定做本机推断事件）

这些事件用于复盘：

* “为什么当时地图上看不到人”
* “掉队是不是因为设备没电/失联”

> 这类事件可以是“本机推断”，不一定来自无线包。

---

### 5) 安全/异常事件（Security/Diagnostics）

你要做强隐私承诺，就必须能复盘“失败原因”，否则用户只会觉得 bug。

* 解密失败（DECRYPT_FAIL）
* key_id 不匹配（KEY_MISMATCH）
* 重放丢弃（REPLAY_DROP）
* 包格式错误（DECODE_FAIL）

这些事件用于复盘：

* “当时收到了包但没显示”的原因

---

## 4) 我现在对你 TEAM 业务的总结边界（我确定 vs 我不会擅自假设）

### 我已经可以确定的（来自你明确提出并反复确认的）

* Team 是临时行动单元，有 Create/Join/Disband
* Team 内位置共享 + waypoint
* 强隐私：channel 隔离 + 应用层 E2EE
* 需要记录整个生命周期事件并可复盘
* 你愿意新增自定义 PortNum（TEAM_*_APP）

### 我不会擅自补的（你没冻结）

* 指令种类到底有哪些、每种指令带哪些参数（我能提建议，但需要你确认）
* 成员健康字段是否要上报（电量、能力、角色）
* 是否要求“全队一致复盘”（你目前更像本地复盘/Leader复盘）

---

# Team 建队 / 加入 报文交互流程（强隐私版本）

## 参与方

```
L = Leader / 建队者设备
M = Member / 队员设备
O = Others / 旁观设备（非 Team 成员）
```

## 信道

```
CH0 = 默认公共频道（Primary Channel）
CHT = Team 私有频道（新建 / 分配的 channel index）
```

## 安全层

```
CH-ENC = Meshtastic 按 Channel 的链路层加密
E2EE   = TeamKey 派生的应用层端到端加密
         （Envelope: TeamEncrypted）
```

---

## 总览：时序线图（ASCII）

```
时间 ↓

L (Leader)                  M (Member)                   O (Others)
────────────────────────────────────────────────────────────────────────

(0) 本地建队（无报文）
L: 生成 TeamKey
L: TeamId = Trunc(Hash(TeamKey))
L: 选择 / 创建 CHT
L: ChannelPSK = KDF(TeamKey, "channel-psk")
L: MgmtKey    = KDF(TeamKey, "mgmt")
L: PosKey     = KDF(TeamKey, "pos")
L: WpKey      = KDF(TeamKey, "wp")

────────────────────────────────────────────────────────────────────────

(1) Team 可被“发现”（不泄密）
CH0
L  ─────── TEAM_ADVERTISE ───────▶  * 
      { team_id, join_hint?, channel_index?, nonce/ts }

                                   O: 只能知道“附近有一个 Team”
                                      无法获得任何密钥或位置

────────────────────────────────────────────────────────────────────────

(2) 队员请求加入（无密钥）
CH0
M  ───── TEAM_JOIN_REQUEST ─────▶  L
      { team_id, member_pub?, nonce/ts }

────────────────────────────────────────────────────────────────────────

(3) Leader 接受加入并下发 Team 配置
CH0
L  ───── TEAM_JOIN_ACCEPT ─────▶  M
      {
        team_id,
        payload = E2EE(MgmtKey, {
                     channel_index = CHT,
                     channel_psk   = ChannelPSK,
                     key_id        = current_key_id,
                     team_params?  (freq / precision / timeout)
                   }),
        nonce/ts
      }

注：
- 此时 M **必须已经通过 UI 获得 TeamKey**
  （短码 / QR / 近距离方式）
- 否则无法解密 MgmtKey / payload

────────────────────────────────────────────────────────────────────────

(4) 队员本地切换到 Team
M: 保存 TeamKey
M: 派生 MgmtKey / PosKey / WpKey
M: 保存 CHT + ChannelPSK
M: 切换到 CHT

────────────────────────────────────────────────────────────────────────

(5) 入队确认（推荐）
CHT (CH-ENC)
M  ───── TEAM_JOIN_CONFIRM ─────▶  L
      {
        team_id,
        payload = E2EE(MgmtKey, {
                     ok,
                     capabilities?,
                     battery?
                   }),
        nonce/seq
      }

────────────────────────────────────────────────────────────────────────

(6) Team 状态同步（可选）
CHT (CH-ENC)
L  ─────── TEAM_STATUS ───────▶  Team
      {
        team_id,
        payload = E2EE(MgmtKey, {
                     member_list_hash,
                     key_id,
                     team_params
                   }),
        nonce/seq
      }

────────────────────────────────────────────────────────────────────────

(7) Team 内位置共享（核心）
CHT (CH-ENC)
Each Member ── TEAM_POSITION_APP ──▶ Team
      payload = TeamEncrypted {
                  team_id,
                  key_id,
                  nonce,
                  ciphertext = AEAD(
                      PosKey,
                      protobuf(meshtastic_Position),
                      aad = header
                  )
               }

Team 外成员：
- 即便拿到 CHT + ChannelPSK
- 只能解 CH-ENC
- 无 TeamKey / PosKey → 无法解密位置
```

---

## 关键语义说明（非常重要）

### 1️⃣ 为什么 **TEAM_ADVERTISE** 不包含密钥

这是**“发现”而不是“加入”**：

* 让人知道「附近有一个 Team」
* 但不授予任何能力
* 防止被动监听就获得权限

---

### 2️⃣ 为什么 **TeamKey 不通过无线明文传播**

这是你“强隐私承诺”的核心：

* TeamKey **只通过 UI 侧渠道**传播（短码 / QR / 近距）
* 无线中只传播 **TeamKey 派生物**
* 即使频道配置外传，也无法逆推出 TeamKey

---

### 3️⃣ 为什么要分 **ChannelPSK / MgmtKey / PosKey**

这是“可演化设计”：

* ChannelPSK：只管“这包是谁能收到”
* MgmtKey：管成员管理 / 参数
* PosKey / WpKey：管位置 / 航点

👉 未来你可以做到：

* 换位置密钥 ≠ 踢人
* 换管理密钥 ≠ 影响历史数据

---

### 4️⃣ 为什么要 **TEAM_JOIN_CONFIRM**

不是为了安全，是为了**产品可感知性**：

* Leader 知道谁真的成功入队
* UI 能显示“成员已就绪”
* 后续 Team 状态才可信

---

## MVP 与增强项划分（帮你控复杂度）

### MVP 必须（第一版就该有）

* (0) 本地建队 + TeamKey 派生
* (1) TEAM_ADVERTISE
* UI 侧 TeamKey 输入 / 扫码
* (3) TEAM_JOIN_ACCEPT
* (4) 切换到 CHT
* (7) TEAM_POSITION_APP（E2EE）

### 可选增强（后续再加）

* member_pub + 更强密钥交换
* TEAM_JOIN_CONFIRM
* TEAM_STATUS
* key rotation / key_id 更新
* 防重放窗口优化

---

# Team Management Protocol

## Message Definitions (Protobuf-Level)

---

## 公共约定（适用于所有 Team 报文）

### Team Identity

* **TeamKey**

  * 高熵随机生成
  * 仅通过 UI 侧（短码 / QR / 近距）分发
  * **MUST NOT** 通过无线明文传播

* **team_id**

  * 定义：`Trunc(Hash(TeamKey))`
  * 用途：标识 Team，而不泄露 TeamKey
  * **MUST** 在所有 Team 相关报文中出现

---

### 加密层级

* **CH-ENC**：Meshtastic Channel 加密
* **E2EE**：应用层 Team 加密（AEAD）

---

### TeamEncrypted Envelope（通用）

> 用于所有 E2EE payload（位置 / 管理 / 状态）

| 字段         | 类型     | 级别   | 说明           |
| ---------- | ------ | ---- | ------------ |
| version    | uint32 | MUST | Envelope 版本  |
| team_id    | bytes  | MUST | Team 标识      |
| key_id     | uint32 | MUST | 当前密钥版本       |
| nonce      | bytes  | MUST | 每包唯一，用于 AEAD |
| ciphertext | bytes  | MUST | AEAD 加密后的数据  |
| aad_flags  | uint32 | MAY  | AAD 类型标识     |

---

## 1. TEAM_ADVERTISE

**用途**：让 Team 可被“发现”，不授予任何能力

* **信道**：CH0
* **加密**：可明文或 CH0 加密
* **PortNum**：`TEAM_MGMT_APP`

### 字段表

| 字段            | 类型     | 级别   | 说明               |
| ------------- | ------ | ---- | ---------------- |
| team_id       | bytes  | MUST | Team 标识          |
| join_hint     | uint32 | MAY  | 加入提示（需确认 / 有效期等） |
| channel_index | uint32 | MAY  | Team Channel 索引  |
| expires_at    | uint64 | MAY  | 广告过期时间           |
| nonce         | bytes  | MUST | 防重放              |

### 语义规则

* **MUST NOT** 包含任何密钥材料
* **MUST NOT** 泄露位置或成员信息
* **MAY** 周期性广播

---

## 2. TEAM_JOIN_REQUEST

**用途**：队员请求加入 Team（无密钥）

* **信道**：CH0
* **PortNum**：`TEAM_MGMT_APP`

### 字段表

| 字段           | 类型     | 级别   | 说明          |
| ------------ | ------ | ---- | ----------- |
| team_id      | bytes  | MUST | 目标 Team     |
| member_pub   | bytes  | MAY  | 公钥（增强密钥交换用） |
| capabilities | uint32 | MAY  | 能力标识        |
| nonce        | bytes  | MUST | 防重放         |

### 语义规则

* **MUST NOT** 包含 TeamKey
* **MAY** 被 Leader 拒绝（无响应）

---

## 3. TEAM_JOIN_ACCEPT

**用途**：Leader 接受成员并下发 Team 配置

* **信道**：CH0
* **PortNum**：`TEAM_MGMT_APP`
* **加密**：E2EE（MgmtKey）

### Payload（解密后结构）

| 字段            | 类型         | 级别   | 说明           |
| ------------- | ---------- | ---- | ------------ |
| channel_index | uint32     | MUST | Team Channel |
| channel_psk   | bytes      | MUST | Channel PSK  |
| key_id        | uint32     | MUST | 当前密钥版本       |
| team_params   | TeamParams | MAY  | 行为参数         |

### 外层字段表

| 字段      | 类型            | 级别   | 说明      |
| ------- | ------------- | ---- | ------- |
| team_id | bytes         | MUST | Team 标识 |
| payload | TeamEncrypted | MUST | E2EE 封装 |
| nonce   | bytes         | MUST | 防重放     |

### 语义规则

* 接收方 **MUST** 已拥有 TeamKey
* 解密失败 **MUST** 丢弃
* **MUST NOT** 在 TeamKey 未确认前应用配置

---

## 4. TEAM_JOIN_CONFIRM

**用途**：成员确认已成功入队

* **信道**：CHT
* **PortNum**：`TEAM_MGMT_APP`
* **加密**：CH-ENC + E2EE（MgmtKey）

### Payload（解密后）

| 字段           | 类型     | 级别   | 说明    |
| ------------ | ------ | ---- | ----- |
| ok           | bool   | MUST | 加入成功  |
| capabilities | uint32 | MAY  | 能力声明  |
| battery      | uint32 | MAY  | 电量百分比 |

---

## 5. TEAM_STATUS

**用途**：同步 Team 当前状态

* **信道**：CHT
* **PortNum**：`TEAM_MGMT_APP`
* **加密**：CH-ENC + E2EE（MgmtKey）

### Payload（解密后）

| 字段               | 类型         | 级别   | 说明     |
| ---------------- | ---------- | ---- | ------ |
| member_list_hash | bytes      | MUST | 成员列表摘要 |
| key_id           | uint32     | MUST | 当前密钥版本 |
| team_params      | TeamParams | MAY  | 当前参数   |

---

## 6. TEAM_POSITION_APP

**用途**：Team 内位置共享（强隐私）

* **信道**：CHT
* **PortNum**：`TEAM_POSITION_APP`
* **加密**：CH-ENC + E2EE（PosKey）

### Payload（E2EE 明文结构）

| 字段              | 类型                  | 级别   | 说明   |
| --------------- | ------------------- | ---- | ---- |
| position        | meshtastic_Position | MUST | 位置数据 |
| precision_level | uint32              | MAY  | 精度等级 |
| timestamp       | uint64              | MUST | 位置时间 |

### 外层（TeamEncrypted）

| 字段         | 类型     | 级别   | 说明      |
| ---------- | ------ | ---- | ------- |
| team_id    | bytes  | MUST | Team 标识 |
| key_id     | uint32 | MUST | 位置密钥版本  |
| nonce      | bytes  | MUST | 每包唯一    |
| ciphertext | bytes  | MUST | AEAD 密文 |

---

## 7. TEAM_WAYPOINT_APP

**用途**：Team 内航点共享

* 与 `TEAM_POSITION_APP` 完全一致
* 使用 `WpKey` 解密
* 明文结构为 `meshtastic_Waypoint`

---

## 8. MUST / SHOULD / MAY 总结（Conformance Checklist）

### MUST

* TeamKey **不得**通过无线明文传播
* 所有 Team 数据 **必须**包含 team_id
* 所有 E2EE payload **必须**使用 AEAD
* nonce **必须** 唯一（防重放）
* 解散 Team 时 **必须** 丢弃所有派生密钥

### SHOULD

* 使用独立的 PosKey / WpKey
* 支持 key_id 轮换
* JOIN_CONFIRM / TEAM_STATUS 用于 UI 同步

### MAY

* 使用 member_pub 做更强密钥交换
* 支持权限分级
* 统一 payload 长度以降低关联风险

---

## 协议一句话定义（可写在文件头）

> **Team 协议定义了一种临时、目标驱动的加密协作单元，其安全边界以 TeamKey 为核心，而非无线信道。**

---


## 目标：你要对用户承诺什么

**隐私承诺（你可以写进 PRD / README）：**

1. **只有 Team 成员能解读 Team 位置与航点**（即便别人拿到频道配置/PSK，也无法解密）
2. **Team 解散后内容不可恢复**（密钥丢弃）
3. **外人无法从包内容推断成员位置**（密文 + 随机化 + 防重放）

---

好，这一步我们把 **Team 的一生**画清楚。
不是“功能流程图”，而是**状态机**——**什么时候处在什么状态、因为什么事件发生跃迁、跃迁时必须做什么**。

下面这张 **ASCII 状态机图**，你可以直接放进协议文档或 PRD。

---

# Team 生命周期状态机（State Machine）

```
                           ┌─────────────────────────┐
                           │         (Idle)          │
                           │     No Active Team      │
                           └───────────┬─────────────┘
                                       │
                          Create Team  │  (UI action)
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────┐
│                         CREATE                              │
│                                                             │
│  Entry actions (local only):                                │
│   - Generate TeamKey (high entropy)                         │
│   - Derive team_id = Trunc(Hash(TeamKey))                   │
│   - Allocate / select Team Channel (CHT)                    │
│   - Derive keys:                                            │
│       ChannelPSK = KDF(TeamKey, "channel-psk")              │
│       MgmtKey    = KDF(TeamKey, "mgmt")                     │
│       PosKey     = KDF(TeamKey, "pos")                      │
│       WpKey      = KDF(TeamKey, "wp")                       │
│                                                             │
│  Exit condition:                                            │
│   - Leader confirms creation                                │
└───────────┬─────────────────────────────────────────────────┘
            │
            │  TEAM_ADVERTISE (CH0)
            │
            ▼
┌─────────────────────────────────────────────────────────────┐
│                         ACTIVE                              │
│                                                             │
│  Team exists and is operational                             │
│                                                             │
│  Ongoing behaviors:                                         │
│   - Broadcast TEAM_ADVERTISE (CH0, optional)                │
│   - Accept TEAM_JOIN_REQUEST (CH0)                          │
│   - Send TEAM_JOIN_ACCEPT (CH0, E2EE MgmtKey)               │
│   - Operate on Team Channel (CHT):                          │
│       * TEAM_JOIN_CONFIRM                                   │
│       * TEAM_STATUS                                         │
│       * TEAM_POSITION_APP (E2EE PosKey)                     │
│       * TEAM_WAYPOINT_APP (E2EE WpKey)                      │
│                                                             │
│  Valid transitions:                                         │
│   - Rotate keys                                             │
│   - Disband team                                            │
└───────────┬───────────────────────┬─────────────────────────┘
            │                       │
            │ Rotate Team Key       │ Disband Team
            │ (Leader action)       │ (Leader or policy)
            │                       │
            ▼                       ▼
┌─────────────────────────────────────────────────────────────┐
│                         ROTATE                              │
│                                                             │
│  Purpose:                                                    │
│   - Mitigate key leakage                                    │
│   - Exclude lost / untrusted members                        │
│                                                             │
│  Entry actions (Leader):                                    │
│   - Increment key_id                                        │
│   - Generate new subkeys:                                   │
│       MgmtKey', PosKey', WpKey'                              │
│                                                             │
│  Protocol actions:                                          │
│   - Broadcast key update via TEAM_STATUS (E2EE MgmtKey)     │
│   - Optionally re-issue TEAM_JOIN_ACCEPT to valid members   │
│                                                             │
│  Member behavior:                                           │
│   - Switch to new key_id                                    │
│   - Drop packets with old key_id                            │
│                                                             │
│  Exit condition:                                            │
│   - All active members synced OR timeout                    │
└───────────┬─────────────────────────────────────────────────┘
            │
            │ Rotation complete
            │
            ▼
┌─────────────────────────────────────────────────────────────┐
│                         ACTIVE                              │
│                   (with new key_id)                         │
└─────────────────────────────────────────────────────────────┘


            (from ACTIVE or ROTATE)
            │
            │ Disband Team
            │
            ▼
┌─────────────────────────────────────────────────────────────┐
│                        DISBAND                              │
│                                                             │
│  Entry actions (Leader):                                    │
│   - Broadcast TEAM_END / final TEAM_STATUS (optional)       │
│                                                             │
│  Mandatory local actions (ALL members):                     │
│   - Immediately discard:                                    │
│       * TeamKey                                             │
│       * ChannelPSK                                          │
│       * MgmtKey / PosKey / WpKey                             │
│   - Stop all Team broadcasts                                │
│   - Leave Team Channel (CHT)                                │
│                                                             │
│  Exit condition:                                            │
│   - Return to Idle                                          │
└───────────┬─────────────────────────────────────────────────┘
            │
            │
            ▼
┌─────────────────────────┐
│         (Idle)          │
│     No Active Team      │
└─────────────────────────┘
```

---

## 状态语义说明（这比图本身更重要）

### 1️⃣ Idle

* 设备**不持有任何 TeamKey**
* 不广播 Team 位置
* UI 显示「未建队 / 未加入」

---

### 2️⃣ Create

* **纯本地状态**
* 没有任何无线安全风险
* 所有“信任根”在这一刻生成

> 这是 **Team 安全边界的诞生点**

---

### 3️⃣ Active

* Team 的 **主要生命周期**
* 所有 Team 功能（地图、位置、指令）都只在此状态合法
* **TeamKey 是唯一信任来源**

---

### 4️⃣ Rotate

* **不是异常状态，是主动防御状态**
* 用来兑现你对用户的这句话：

  > “即使配置外传，也能立刻止损”

关键点：

* Rotate **不改变 team_id**
* 只改变 `key_id` + 派生密钥
* 旧密钥立即失效

---

### 5️⃣ Disband

* **安全终止态**
* 所有密钥必须被销毁
* 不允许“恢复”“回看”“重连”

> Team 结束 ≠ 暂停
> Team 结束 = **密码学意义上的死亡**

---

## 你可以直接写进协议的 MUST 规则（来自状态机）

* **MUST** 仅在 `ACTIVE` / `ROTATE` 状态发送 Team 数据
* **MUST** 在 `DISBAND` 进入时销毁全部 Team 密钥
* **MUST** 丢弃 key_id 不匹配的数据包
* **MUST NOT** 从 `DISBAND` 回到 `ACTIVE`（必须重新建队）

---

## 为什么这张状态机“站得住”

* **产品层**：每个状态都能映射到一个清晰 UI
* **协议层**：每个跃迁都有明确报文或本地动作
* **安全层**：密钥生命周期与状态生命周期完全一致

---

非常好，这一步其实是**把“用户点了什么”和“协议里发生了什么”一一对齐**。
下面我会**严格按 UI 行为 → 状态变化 → 报文触发**来写，不引入新概念、不模糊。

你可以把这份内容理解为：

> **UI 是状态机的控制面板，协议报文是状态跃迁的副作用。**

---

# UI 行为 ↔ Team 状态机 ↔ 报文触发 精确映射

---

## 一、UI 总览：Team 菜单的三种“用户可感知状态”

从 UI 角度，Team 其实只有三种状态（和协议状态一一对应）：

| UI 显示状态  | 协议状态    | 用户能做什么       |
| -------- | ------- | ------------ |
| 未建队      | Idle    | 建队 / 加入      |
| Team 进行中 | Active  | 查看 / 分享 / 解散 |
| 正在解散     | Disband | 无（系统动作）      |

> Rotate 是 **Active 内的子动作**，通常是 Leader-only，不是普通用户主流程。

---

## 二、UI 行为 1：点击「建队（Create Team）」

### UI 行为

```
菜单 → Team → [Create Team]
```

---

### 对应状态机跃迁

```
Idle ──(Create Team)──▶ Create ──▶ Active
```

---

### 报文与动作映射（逐步）

#### Step 1：用户点击「Create Team」（无报文）

**UI 行为**

* 用户确认“我要建一个 Team”

**本地动作（MUST）**

* 生成 `TeamKey`
* 计算 `team_id = Trunc(Hash(TeamKey))`
* 分配 / 选择 `CHT`
* 派生密钥：

  * `ChannelPSK`
  * `MgmtKey`
  * `PosKey`
  * `WpKey`

📌 **此时没有任何无线报文**
📌 **这是 Team 安全边界的起点**

---

#### Step 2：UI 显示「Team 已创建 / 分享码」

**UI 行为**

* 显示 Team Code / QR
* 显示「成员数：1」

**协议动作**

* **进入 Active 状态**
* 开始周期性（或一次性）广播：

```
CH0 → TEAM_ADVERTISE
```

```text
TEAM_ADVERTISE {
  team_id,
  join_hint?,
  channel_index?,
  nonce
}
```

📌 这是 **UI 显示“Team 已存在”** 的唯一协议依据

---

#### Step 3：UI 返回 Team 主页面（Active）

**UI 行为**

* 显示：

  * Team Active
  * Members: 1
  * [View Team Map]
  * [Share Team]
  * [Disband Team]

**协议行为**

* 允许并处理以下报文：

  * `TEAM_JOIN_REQUEST`
  * `TEAM_JOIN_ACCEPT`
  * `TEAM_POSITION_APP`

---

## 三、UI 行为 2：他人加入 Team（Join Team）

### UI 行为（队员）

```
Team → Join Team → 输入短码 / 扫码 → Confirm
```

---

### 报文与状态映射

#### Step 1：队员输入短码 / 扫码（无报文）

**UI 行为**

* 用户确认加入

**本地动作（MUST）**

* 解析 / 获得 `TeamKey`
* 预计算 `team_id`
* 派生 `MgmtKey / PosKey / WpKey`

📌 **没有 TeamKey，后续任何 JOIN 报文都没有意义**

---

#### Step 2：队员请求加入

```
CH0 → TEAM_JOIN_REQUEST
```

```text
TEAM_JOIN_REQUEST {
  team_id,
  nonce
}
```

---

#### Step 3：Leader UI 出现「加入请求」

**UI 行为（Leader）**

* 显示“新成员请求加入”
* [Accept] / [Ignore]

📌 UI 事件 **直接绑定** 下一条报文是否发送

---

#### Step 4：Leader 点击「Accept」

```
CH0 → TEAM_JOIN_ACCEPT
```

```text
TEAM_JOIN_ACCEPT {
  team_id,
  payload = E2EE(MgmtKey, {
    channel_index,
    channel_psk,
    key_id,
    team_params?
  }),
  nonce
}
```

---

#### Step 5：队员 UI 切换为「Team Active」

**UI 行为**

* 显示 Team 主页面
* Members ≥ 2

**本地动作**

* 保存 CHT + ChannelPSK
* 切换到 CHT

**协议动作（推荐）**

```
CHT → TEAM_JOIN_CONFIRM
```

---

## 四、UI 行为 3：Team 进行中（无按钮，但有持续行为）

### UI 行为

```
Team Active → View Team Map
```

---

### 协议行为（持续）

* 每个成员周期性发送：

```
CHT → TEAM_POSITION_APP
```

* UI 地图刷新 **只依赖成功解密后的 payload**
* 解密失败的数据包：

  * MUST 被丢弃
  * MUST NOT 更新 UI

---

## 五、UI 行为 4：点击「解散 Team（Disband Team）」【Leader】

### UI 行为

```
Team Active → [Disband Team] → Confirm
```

---

### 对应状态机跃迁

```
Active ──(Disband)──▶ Disband ──▶ Idle
```

---

### 报文与动作映射

#### Step 1：Leader 点击「Disband」

**UI 行为**

* 显示强确认弹窗

---

#### Step 2：Leader 确认解散

**协议动作（可选但推荐）**

```
CHT → TEAM_STATUS / TEAM_END
```

```text
TEAM_STATUS {
  team_id,
  payload = E2EE(MgmtKey, {
    state = DISBAND
  })
}
```

📌 这是 **通知性质**，不是安全关键
📌 即使收不到，也不影响安全性

---

#### Step 3：所有设备进入 Disband

**本地动作（MUST，所有成员）**

* 立即销毁：

  * `TeamKey`
  * `ChannelPSK`
  * `MgmtKey / PosKey / WpKey`
* 停止所有 Team 报文
* 离开 CHT

---

#### Step 4：UI 回到未建队状态

```
Team → You are not in a Team
[Create Team] [Join Team]
```

📌 **UI 是密钥销毁完成的“可视确认”**

---

## 六、UI 行为 5：普通成员「离开 Team（Leave）」【非解散】

> 这是 **UI 行为 ≠ 协议状态变化** 的典型例子

### UI 行为

```
Team Active → [Leave Team]
```

---

### 本地动作（MUST）

* 销毁所有 TeamKey / 派生密钥
* 离开 CHT

### 协议动作（MAY）

```
CHT → TEAM_LEAVE_NOTICE
```

> 仅用于 UI 同步，不影响 Team 存续

📌 **Team 状态仍为 Active（对其他人）**

---

## 七、关键设计原则（可以写进 PRD）

1. **UI 行为永远先于报文**
   报文只是“用户意图的副作用”

2. **没有 TeamKey → UI 不允许进入 Active**
   安全失败必须表现为“操作不可达”

3. **解散是密钥行为，不是通知行为**
   报文失败 ≠ 解散失败

---

## 一句话总结这套映射关系

> **UI 决定状态，
> 状态决定密钥，
> 密钥决定哪些报文“有意义”。**

---

### 下一步（非常自然）

如果你愿意，我可以下一步帮你直接做其中之一：

1. **把这套映射写成“UI × Protocol 对照表”**（方便测试与 PR）
2. **帮你设计 Team 页面在不同状态下的 LVGL 组件树**
3. **把这些 UI 行为转成自动化测试用例描述**

你现在这一步，已经是在做**系统级产品设计**了，而不是“功能拼装”。


好，这一步我们**完全站在“嵌入式 UI 工程”的角度**来做。
目标不是“好看”，而是 **状态清晰、组件稳定、不会演化失控**。

下面我给你的是 **LVGL 组件树（Component Tree）**，不是布局草图；
你可以直接据此拆 `.cpp / .h`，或者对照你现在的 UI 架构来重构。

---

# Team 页面 · LVGL 组件树设计

（按 **状态** 明确分支，而不是条件 if/else 堆在一起）

---

## 总体设计原则（先给你“架构约束”）

### 1️⃣ Team 页面 = 状态容器（不是功能容器）

```text
TeamPage
└── TeamStateContainer   // 只负责：Idle / Active / Disband
```

* **永远只挂载一个子树**
* 状态切换 = 删除旧子树 + 创建新子树
* 不在同一树里 hide/show

👉 这是防止 LVGL 页面“变成屎山”的关键

---

### 2️⃣ 页面分三棵互斥的树

```text
TeamState = { IDLE, ACTIVE, DISBAND }
```

* `IdleTree`
* `ActiveTree`
* `DisbandTree`（瞬态，通常很短）

---

## 一、Team 页面根节点（所有状态共用）

```text
TeamPage (lv_obj_t*)
├── TopBar
│   ├── BackButton
│   └── TitleLabel ("Team")
│
└── TeamStateContainer (lv_obj_t*)
    └── <StateSpecificTree>
```

说明：

* `TopBar` 永远存在
* `TeamStateContainer` 是**唯一可变区域**

---

## 二、状态 1：未建队（IDLE）

### 用户心智

> “我现在不在任何 Team 里。”

---

### 组件树：IdleTree

```text
IdleTree (lv_obj_t*)
├── CenterContainer
│   ├── StatusIcon        // 简单图标：空队伍
│   ├── StatusLabel       // "You are not in a Team"
│   └── Spacer
│
├── ActionContainer
│   ├── CreateTeamButton  // 主按钮
│   └── JoinTeamButton    // 次按钮
```

---

### 组件职责说明

* `CreateTeamButton`

  * 点击 → **触发 UI 行为：Create Team**
  * 后续状态切换由控制器决定

* `JoinTeamButton`

  * 跳转到 Join 流程页（输入码 / 扫码）
  * 成功后切换到 `ACTIVE`

📌 **IdleTree 不关心任何协议或密钥**

---

## 三、状态 2：Team 进行中（ACTIVE）

这是 **停留时间最长、最重要的状态**。

### 用户心智

> “我在一个 Team 里，我能看到状态，也能做决定。”

---

### 组件树：ActiveTree（完整）

```text
ActiveTree (lv_obj_t*)
├── SummaryCard
│   ├── TeamStatusLabel     // "Team Active"
│   ├── MemberCountLabel   // "Members: N"
│   └── PrivacyBadge       // "Strong Privacy / E2EE"
│
├── Divider
│
├── ActionList
│   ├── ViewMapItem        // → Team Map
│   ├── ShareTeamItem      // → 显示 Team Code / QR
│   └── (optional) RotateKeyItem  // Leader only
│
├── Divider
│
└── DangerZone
    └── DisbandButton / LeaveButton
```

---

### 关键：Leader / Member 的分支（不是不同页面）

#### 普通成员（Member）

```text
DangerZone
└── LeaveTeamButton
```

#### Leader（建队者）

```text
DangerZone
└── DisbandTeamButton
```

> ⚠️ **不要在 UI 层暴露“Leader”概念**
> 只通过按钮存在与否体现

---

### 各组件的“协议触发点”

#### `ViewMapItem`

* 只做一件事：跳转到 Team Map 页面
* Team Map 页面只消费：

  * `TEAM_POSITION_APP`
  * `TEAM_WAYPOINT_APP`

#### `ShareTeamItem`

* 进入 **Share Subpage**
* 只读数据：

  * Team Code
  * QR
* ❌ 不触发任何报文

#### `RotateKeyItem`（Leader-only）

* 点击 → 进入确认页
* 确认后：

  * 触发 **Rotate 状态**
  * 发送 `TEAM_STATUS (new key_id)`

#### `DisbandTeamButton`

* 点击 → 进入 **DisbandTree**

---

## 四、状态 3：解散中（DISBAND）

这是一个 **瞬态状态**，但非常重要。

### 用户心智

> “这件事是不可逆的。”

---

### 组件树：DisbandTree

```text
DisbandTree (lv_obj_t*)
├── WarningIcon
├── WarningLabel
│   └── "Disbanding Team..."
│
└── ProgressIndicator
```

---

### 行为绑定（关键）

* **进入 DisbandTree 的同时**

  * 本地立即销毁：

    * TeamKey
    * ChannelPSK
    * 所有派生密钥
  * 停止 Team 报文

* **不等待网络结果**

* UI 停留时间极短（例如 300–500ms）

---

### Disband 完成后

```text
DISBAND → IDLE
```

* 销毁 `DisbandTree`
* 创建 `IdleTree`

---

## 五、状态切换 = 组件树切换（推荐实现模式）

### 推荐控制逻辑（伪代码）

```cpp
void TeamPage::setState(TeamState newState) {
    if (currentTree) {
        lv_obj_del(currentTree);
        currentTree = nullptr;
    }

    switch (newState) {
    case IDLE:
        currentTree = buildIdleTree(container);
        break;
    case ACTIVE:
        currentTree = buildActiveTree(container);
        break;
    case DISBAND:
        currentTree = buildDisbandTree(container);
        break;
    }
}
```

📌 **没有 show/hide**
📌 **没有 if (isLeader) scattered everywhere**
📌 **所有状态边界在一个地方**

---

## 六、这套组件树设计的三个“硬优势”

1. **状态与 UI 完全同构**
   状态机 = UI 结构，不是注释

2. **协议失败不会污染 UI**

   * 解散不依赖报文成功
   * UI 只反映本地密钥状态

3. **未来扩展不会炸**

   * 加 Team Settings
   * 加 Key Rotation
   * 加 Debug Info
     都只是在 `ActiveTree` 里加一支

---你这个“拉回”的动作非常对。
我们现在**明确只做一件事**：

> **围绕「建队状态机 + 密钥交换流程」，哪些数据必须持久化？哪些绝不能持久化？哪些是状态性的？**

不谈 UI、不谈地图、不谈未来扩展，只谈**数据与生命周期**。

---

# Team 建队 & 密钥交换阶段

## 数据持久化整理（严格按状态机）

---

## 一、先给结论（总表）

> **不是所有参与建队的东西都应该持久化**
> 持久化的唯一判断标准是：
> 👉 *“设备重启后，是否还能继续处于同一个 Team 状态”*

### 数据分三类：

| 类别         | 是否持久化 | 原因            |
| ---------- | ----- | ------------- |
| Team 身份与密钥 | ✅ 必须  | 否则重启=自动退队     |
| Channel 配置 | ✅ 必须  | 否则无法接收 Team 包 |
| 协议瞬态状态     | ❌ 不应  | 可通过报文重建       |
| UI / 交互状态  | ❌ 不应  | 完全是表现层        |

---

## 二、按状态机逐状态分析

---

## 状态 0：Idle（未建队）

### 持久化数据

**无。**

```text
(no Team-related persistent data)
```

### 原则

* Idle 状态下 **设备上不存在任何 Team 痕迹**
* 这是安全与心理边界的起点

---

## 状态 1：Create（本地建队，尚未 Active）

> 这是一个**非常短暂的状态**
> 只存在于一次 UI 操作中

### 是否需要持久化？

❌ **不需要**

### 原因

* Create 是一次**原子操作**
* 要么成功进入 Active
* 要么失败回到 Idle

👉 **Create 阶段失败 = 不留下任何痕迹**

---

## 状态 2：Active（Team 正在进行）

这是**唯一需要持久化的核心状态**。

---

### 1️⃣ Team 身份类（必须持久化）

```text
TeamIdentity
├── team_id          (bytes)
├── team_role        (enum: LEADER / MEMBER)
├── key_id           (uint32)
```

#### 说明

* `team_id`：所有 Team 数据的索引键
* `team_role`：影响 UI 与允许的动作
* `key_id`：当前密钥版本（用于解密判断）

✅ **MUST persist**

---

### 2️⃣ Team 密钥类（必须持久化，安全存储）

```text
TeamSecrets
├── team_key         (bytes)   // 根密钥
├── mgmt_key         (bytes)
├── pos_key          (bytes)
├── wp_key           (bytes)
```

#### 说明

* 派生密钥 **可以重算**，但不建议
* 重算依赖 KDF 版本一致，风险高
* 实践中直接存派生结果更稳

✅ **MUST persist**
⚠️ **必须使用安全存储（NVS / encrypted storage）**

---

### 3️⃣ Channel 配置类（必须持久化）

```text
TeamChannelConfig
├── channel_index    (uint8 / uint32)
├── channel_psk      (bytes)
```

#### 说明

* 重启后必须能重新监听 CHT
* 否则会“逻辑上还在 Team，物理上听不到”

✅ **MUST persist**

---

### 4️⃣ Team 行为参数（建议持久化）

```text
TeamParams
├── position_interval_ms
├── position_precision_level
├── advertise_enabled
├── join_policy
```

#### 说明

* 这些参数**由 Leader 决定**
* 成员端只是执行

🟡 **SHOULD persist**
（不存也能跑，但体验不一致）

---

### 5️⃣ 不应持久化的东西（非常重要）

#### ❌ 协议瞬态状态

```text
DO NOT persist:
- 已发送但未确认的 JOIN_REQUEST
- JOIN_CONFIRM 状态
- 最近一次 TEAM_STATUS 内容
- 成员列表（可通过广播重建）
```

原因：

* 都是 **软状态**
* 断电/重启后自然恢复

---

#### ❌ UI 状态

```text
DO NOT persist:
- 当前是否在 Team 页面
- 是否展开某个子菜单
- 是否刚刚显示过 QR
```

---

## 状态 3：Rotate（密钥轮换中）

> Rotate 是 **Active 的子状态**

### 持久化策略：**两阶段提交**

#### 临时状态（不持久化）

```text
RotateContext (RAM only)
├── new_key_id
├── new_mgmt_key
├── new_pos_key
├── new_wp_key
```

#### 切换成功瞬间（持久化）

```text
TeamSecrets (overwrite)
├── key_id = new_key_id
├── mgmt_key = new_mgmt_key
├── pos_key  = new_pos_key
├── wp_key   = new_wp_key
```

📌 **不要持久化“正在轮换中”**
📌 **要么成功覆盖，要么保持旧密钥**

这是**避免掉电/异常导致“半轮换”**的关键。

---

## 状态 4：Disband（解散）

### 行为规则（强制）

> **Disband = 数据销毁**

### 必须执行的持久化动作

```text
DELETE persistent:
- TeamIdentity
- TeamSecrets
- TeamChannelConfig
- TeamParams
```

### 不允许的行为

* ❌ 不允许标记“已解散但可恢复”
* ❌ 不允许保留 team_id
* ❌ 不允许缓存旧密钥

📌 **Disband 是密码学意义上的死亡**

---

## 三、Join 流程中的持久化时机（精确）

### 成员设备（Member）

| 时刻                   | 是否持久化 | 内容                               |
| -------------------- | ----- | -------------------------------- |
| 输入短码                 | ❌     | （仅 RAM）                          |
| 发送 JOIN_REQUEST      | ❌     |                                  |
| 收到 JOIN_ACCEPT 并成功解密 | ✅     | TeamIdentity + Secrets + Channel |
| 切换到 CHT 成功           | ✅     | 同上                               |
| JOIN_CONFIRM         | ❌     |                                  |

---

### 建队者（Leader）

| 时刻          | 是否持久化 | 内容         |
| ----------- | ----- | ---------- |
| Create Team | ✅     | 全量 Team 数据 |
| 接受新成员       | ❌     |            |
| Rotate 成功   | ✅     | 覆盖密钥       |
| Disband     | ❌（删除） |            |

---

## 四、你可以直接写进协议/实现文档的一句话规则

> **只有“定义 Team 身份连续性的数据”才允许持久化；
> 所有报文级、交互级状态都必须是可丢弃的。**

---

## 五、一个非常实用的工程建议（重要）

**把 Team 持久化做成一个“单一对象”**

```text
PersistentTeamState
├── identity
├── secrets
├── channel
├── params
```

* 有这个对象 → Team = Active
* 没有这个对象 → Team = Idle

📌 **不要拆散到多个 KV**
📌 **不要用 flag 表示“在不在 Team”**

---

# 1️⃣ 建队流程（Leader）

## 持久化读写时序图

```
时间 ↓

Leader Device
──────────────────────────────────────────────────────────────

[UI] Click "Create Team"
        |
        v
📦 RAM: generate TeamKey
📦 RAM: derive team_id, ChannelPSK, MgmtKey, PosKey, WpKey
📦 RAM: select Team Channel (CHT)

        |
        |  (still NOT persistent)
        |
        v
[UI] Confirm Create
        |
        v
💾 PERSIST: write PersistentTeamState
    ├── identity.team_id
    ├── identity.role = LEADER
    ├── identity.key_id = 0
    ├── secrets.team_key
    ├── secrets.mgmt_key
    ├── secrets.pos_key
    ├── secrets.wp_key
    ├── channel.channel_index = CHT
    ├── channel.channel_psk
    └── params (optional)

        |
        v
[STATE] Team = ACTIVE
        |
        v
[PROTO] start TEAM_ADVERTISE (CH0)
[PROTO] accept JOIN_REQUEST
[PROTO] send JOIN_ACCEPT
```

### 关键规则（必须遵守）

* ❌ **在 UI Confirm 之前，绝不写 Flash**
* ✅ **第一次持久化 = Team 正式存在**
* 💡 掉电在 Confirm 之前 → 自动回到 Idle（无残留）

---

# 2️⃣ 加入流程（Member）

## 持久化读写时序图（最容易出 bug 的地方）

```
时间 ↓

Member Device
──────────────────────────────────────────────────────────────

[UI] Click "Join Team"
        |
        v
[UI] Input Code / Scan QR
        |
        v
📦 RAM: obtain TeamKey
📦 RAM: derive team_id, MgmtKey, PosKey, WpKey

        |
        |  (still NOT persistent)
        |
        v
[PROTO] send TEAM_JOIN_REQUEST (CH0)
        |
        v
[PROTO] receive TEAM_JOIN_ACCEPT (CH0)

        |
        v
[SEC] try decrypt JOIN_ACCEPT payload
        |
        |-- decryption FAIL --> ❌ abort (NO write)
        |
        v
📦 RAM: extract channel_index, channel_psk, key_id

        |
        |  (still NOT persistent)
        |
        v
[PROTO] switch to Team Channel (CHT)
        |
        |-- switch FAIL --> ❌ abort (NO write)
        |
        v
💾 PERSIST: write PersistentTeamState
    ├── identity.team_id
    ├── identity.role = MEMBER
    ├── identity.key_id
    ├── secrets.team_key
    ├── secrets.mgmt_key
    ├── secrets.pos_key
    ├── secrets.wp_key
    ├── channel.channel_index
    ├── channel.channel_psk
    └── params (optional)

        |
        v
[STATE] Team = ACTIVE
        |
        v
[PROTO] send TEAM_JOIN_CONFIRM (CHT)
[PROTO] start TEAM_POSITION_APP
```

### 关键规则（这是重点）

* ❌ **收到 JOIN_ACCEPT ≠ 可以写 Flash**
* ❌ **解密成功 ≠ 可以写 Flash**
* ✅ **只有在“成功切换到 CHT”之后才允许持久化**

> 这是为了防止：
> **“Flash 里记录着 Team，但无线层根本进不了 Team Channel”**

---

# 3️⃣ 解散流程（Disband）

## 持久化删除时序图（最安全的一条）

### 3A. Leader 解散 Team

```
时间 ↓

Leader Device
──────────────────────────────────────────────────────────────

[UI] Click "Disband Team"
        |
        v
[UI] Confirm Disband
        |
        v
❌ DELETE: erase PersistentTeamState
    ├── identity
    ├── secrets
    ├── channel
    └── params

        |
        v
📦 RAM: clear all Team-related state
        |
        v
[STATE] Team = IDLE
        |
        v
[PROTO] (optional) send TEAM_STATUS / TEAM_END
```

### 3B. 普通成员离开 Team（Leave）

```
时间 ↓

Member Device
──────────────────────────────────────────────────────────────

[UI] Click "Leave Team"
        |
        v
❌ DELETE: erase PersistentTeamState
        |
        v
📦 RAM: clear Team state
        |
        v
[STATE] Team = IDLE
        |
        v
[PROTO] (optional) send LEAVE_NOTICE
```

### 关键规则（必须统一）

* ❌ **不要先发报文再删数据**
* ❌ **不要等待网络 ACK**
* ✅ **删除持久化 = Team 立刻不存在**

> 报文是“礼貌”，
> **删除密钥才是“事实”。**

---

# 4️⃣ 旋转密钥（Rotate）补充：持久化原子性

```
Leader Device
──────────────────────────────────────────────────────────────

[UI] Click "Rotate Key"
        |
        v
📦 RAM: generate new subkeys
📦 RAM: new_key_id

        |
        v
[PROTO] broadcast TEAM_STATUS(new_key_id)

        |
        |-- timeout / abort --> ❌ discard RAM only
        |
        v
💾 PERSIST (ATOMIC overwrite):
    ├── identity.key_id = new_key_id
    ├── secrets.mgmt_key = new
    ├── secrets.pos_key  = new
    └── secrets.wp_key   = new

        |
        v
[STATE] Team still ACTIVE (new key)
```

### 关键规则

* ❌ **不允许“半写入”**
* ❌ **不持久化 Rotate 进行中状态**
* ✅ **要么全换，要么不换**

---

# 5️⃣ 一条可以写进实现规范的铁律

> **持久化只发生在“状态跃迁完成之后”，
> 删除只发生在“状态跃迁开始之时”。**

---

# 6️⃣ 最终工程检查清单（你可以直接用）

* [ ] Create Confirm 前没有任何 Flash 写入
* [ ] Join 成功切换 CHT 前没有任何 Flash 写入
* [ ] Disband / Leave 第一动作就是删除持久化
* [ ] Rotate 只有一个原子写点
* [ ] PersistentTeamState 是否是“单一对象”

---

# Team 持久化存储设计

## NVS / Flash Layout & Wear Strategy

> 设计目标：
>
> * 掉电安全
> * 原子更新
> * 最小擦写
> * 明确“Team 存在 / 不存在”的判据

---

## 一、总原则（先立铁律）

### 1️⃣ Team 持久化 = **单一对象**

> **Flash 中只有一个“Team 是否存在”的真相源**

```text
PersistentTeamState
```

* 有它 → Team = Active
* 没有它 → Team = Idle

❌ 不使用多个 flag
❌ 不在不同 namespace 里拆散

---

### 2️⃣ 写入次数极少，是“生命周期级别”的

| 操作          | 写 Flash？ |
| ----------- | -------- |
| 建队成功        | ✅ 一次     |
| 成员加入成功      | ✅ 一次     |
| Rotate Key  | ✅ 一次     |
| 正常使用（位置/聊天） | ❌        |
| 解散 / 离开     | ✅ 删除     |

> **不是高频写场景**
> 所以重点是 **正确性 > 极限性能**

---

## 二、推荐存储方式（ESP32 实践）

### ✅ 推荐：NVS（Non-Volatile Storage）

原因：

* 自带 wear leveling
* 支持 blob
* 支持 namespace
* 原子语义清晰

> 你现在的需求 **完全不需要自定义 raw flash**

---

## 三、NVS Namespace 设计

```text
NVS Namespace: "team"
```

**只有这一个 namespace 存 Team。**

---

## 四、Key 布局（极简但完整）

### 核心 Key 列表

| Key          | 类型   | 说明                     |
| ------------ | ---- | ---------------------- |
| `version`    | u32  | 结构版本                   |
| `team_state` | blob | 整个 PersistentTeamState |

> ❗**不要拆成几十个 key**
> 拆了就很难保证一致性与原子性

---

## 五、PersistentTeamState 结构（Flash 中的唯一真相）

### 逻辑结构（与你之前分析一致）

```text
PersistentTeamState
├── header
│   ├── magic
│   ├── version
│   ├── length
│   └── crc32
│
├── identity
│   ├── team_id
│   ├── role          (LEADER / MEMBER)
│   └── key_id
│
├── secrets
│   ├── team_key
│   ├── mgmt_key
│   ├── pos_key
│   └── wp_key
│
├── channel
│   ├── channel_index
│   └── channel_psk
│
└── params (optional)
    ├── position_interval_ms
    ├── precision_level
    └── flags
```

---

### Header 设计（非常重要）

| 字段        | 作用          |
| --------- | ----------- |
| `magic`   | 判断是否存在 Team |
| `version` | 结构版本        |
| `length`  | 防截断         |
| `crc32`   | 掉电 / 半写检测   |

📌 **header 是你对抗掉电与损坏的最后防线**

---

## 六、写入策略（什么时候写，怎么写）

### 1️⃣ 写入只发生在“成功跃迁后”

你前面定的规则，现在变成存储规则：

| 场景              | 写入内容                    |
| --------------- | ----------------------- |
| Create Confirm  | 写完整 PersistentTeamState |
| Join 成功切换 CHT   | 写完整 PersistentTeamState |
| Rotate 成功       | **整体覆盖写**               |
| Disband / Leave | 删除 key                  |

---

### 2️⃣ 写入方式：整体覆盖（不是增量）

```text
nvs_set_blob("team", "team_state", &state, sizeof(state))
nvs_commit()
```

* 不修改子字段
* 不做 patch
* 不维护“正在 rotate”状态

📌 **整体写 + CRC = 最稳**

---

## 七、擦写 / 删除策略（关键）

### 解散 / 离开 Team

```text
nvs_erase_key("team", "team_state")
nvs_commit()
```

* **删除即事实**
* 不保留“历史 Team”
* 不留可恢复路径

---

### 为什么不用 flag 表示“已解散”

因为：

* flag 本身也是状态
* flag 需要维护一致性
* flag 会引入“幽灵 Team”

**不存在 = 最干净**

---

## 八、掉电与损坏恢复策略

### 启动时的判断逻辑（唯一入口）

```text
if (nvs_has_key("team", "team_state")) {
    read blob
    if (magic ok && crc ok) {
        Team = ACTIVE
    } else {
        erase key
        Team = IDLE
    }
} else {
    Team = IDLE
}
```

📌 **任何异常 → 删除 → Idle**

---

## 九、Key Rotation 的原子性保证

### 正确做法（你前面已经定了）

1. 新密钥 **只存在 RAM**
2. 广播 Rotate
3. 同步完成
4. **一次性覆盖写 PersistentTeamState**

```text
old_state → new_state
nvs_set_blob(...)
nvs_commit()
```

❌ 不写中间态
❌ 不写 `rotating = true`

---

## 十、Flash 磨损分析（给你信心）

### 最坏情况估算

| 操作     | 次数      |
| ------ | ------- |
| 建队     | 1       |
| Rotate | 假设 20 次 |
| 解散     | 1       |

**< 50 次写入 / Team 生命周期**

NVS 默认 wear leveling 足以支撑 **数十万次**。

👉 **可以放心**

---

## 十一、一个非常重要的安全建议（强烈）

### TeamSecrets 存储

* 如果平台支持：

  * 使用 **NVS encrypted partition**
* 或：

  * 在写入前，用 device-unique key 再包一层

你不需要军工级，只需要：

> **Flash 被 dump，TeamKey 不可直接读出**

---

## 十二、最终“实现检查清单”（你可以直接用）

* [ ] Team 只占用一个 NVS key
* [ ] PersistentTeamState 有 magic + crc
* [ ] 所有写入都是整体 blob
* [ ] 解散 = erase key
* [ ] 启动异常 = erase key
* [ ] Rotate 只有一个 commit 点

---

## 一句话总结这套设计

> **Flash 里不保存“过程”，
> 只保存“事实”。**

---

## 1) 复盘的三种级别（先定产品边界）

你要“整个生命周期所有事件落地”，但落地可以有不同强度：

### A. 本地复盘（推荐默认）

* **每台设备只记录自己看到/发出的事件**
* 不需要全员上报
* 最少协议改动
* 复盘不一定 100%完整（取决于你当时是否收到包）

### B. 领队完整复盘（可选增强）

* Leader 设备尽量收集全队事件（仍然是“尽量”，无线环境无法保证 100%）
* 更接近“行动记录仪”

### C. 全队一致复盘（最重，不建议一开始做）

* 需要全队 ACK/补齐/对账
* 立刻把系统复杂度拉爆

> 我建议：**先做 A**，再做 B（Leader 模式），C 暂时不碰。

---

## 2) 你要记录什么：事件模型（Event Sourcing 风格）

把 Team 的一生理解为：**事件序列 + 状态重建**。

### 事件必须包含的元信息（所有事件通用）

* `event_id`：唯一（可用 `sender_id + seq`）
* `team_id`
* `ts`：事件发生时间（设备本地时间 + 可选 mesh 时间）
* `sender_id`：节点 ID
* `event_type`
* `payload`：按类型携带字段
* `key_id`：当时使用的密钥版本（很重要，便于复盘解密）

### 事件类型建议（覆盖你当前系统）

**生命周期**

* `TEAM_CREATED`（Leader 本地）
* `TEAM_ADVERTISE_SENT/RECEIVED`
* `TEAM_JOIN_REQUEST_SENT/RECEIVED`
* `TEAM_JOIN_ACCEPT_SENT/RECEIVED`
* `TEAM_JOIN_CONFIRM_SENT/RECEIVED`
* `TEAM_STATUS_SENT/RECEIVED`
* `TEAM_KEY_ROTATED`（含 old/new key_id）
* `TEAM_DISBANDED` / `TEAM_LEFT`

**行动**

* `TEAM_POSITION_RX`（建议只存“采样后的”位置点）
* `TEAM_WAYPOINT_RX`
* `TEAM_COMMAND_RX`（你未来的集合/求助等指令）

**异常**

* `DECRYPT_FAIL`（重要：复盘时能解释“为什么当时没显示”）
* `REPLAY_DROP`
* `MEMBER_LOST` / `MEMBER_RECOVERED`

---

## 3) 日志存储位置：Flash vs SD 的现实选择

### 如果你有 SD 卡（强烈推荐）

* 用 SD 文件做**追加写日志**（Flash 磨损问题直接变小）
* 文件系统好用（导出、查看、同步）

### 只有 Flash（NVS/自定义分区）

* 也能做，但要用**环形日志（ring buffer）**
* 写放大、磨损要精心控制
* 不适合存“高频位置点”

> 结论：**位置轨迹复盘几乎必然要 SD**，否则你必须极度降采样。

---

## 4) 最关键：复盘与“解散销毁密钥”如何兼容？

你之前的安全模型是：Disband 立即销毁 TeamKey → 历史不可读。
而复盘要求：解散后仍能读历史。

所以你要引入一个独立的日志密钥：

### 新增密钥：`LogKey`

* `LogKey = KDF(TeamKey, "log")` **可以**，但如果 Disband 会销毁 TeamKey，那 LogKey 也丢了。
* 更合理：**创建 Team 时生成独立随机 LogKey**，并设置“是否保留”的策略。

### 两种策略（对应产品开关）

1. **默认：Disband 销毁 LogKey**

   * 不可复盘
   * 最强隐私承诺（默认建议）

2. **开启复盘：Disband 保留 LogKey（仅本机）**

   * 日志文件仍然是加密的
   * 只有该设备能复盘
   * 你仍可承诺：**TeamKey 销毁，网络权限消失；但本机保留行动记录**

这其实很符合户外真实需求：

* **通信权限**随队伍结束而结束
* **行动记录**可由个人/领队保留

---

## 5) 写入策略：怎样既完整又不爆存储？

### 位置点不要“每包都落盘”

位置是最高频数据，必须做降采样：

* 规则一：**按时间采样**（例如每 10s/30s 记录一次）
* 规则二：**按距离采样**（移动超过 20m 才记）
* 规则三：异常优先（SOS/集合时临时升频）

### 事件日志推荐结构：Append-only + Checkpoint

为了复盘速度与稳定性：

* `events.log`：追加写（每条事件一条记录）
* `snapshot.bin`：偶尔写一个“当前 Team 状态快照”（比如每 5 分钟或每 200 条事件）

  * 复盘时：从最近快照开始回放事件，速度快

---

## 6) 存储格式建议（别纠结，选一个能跑的）

你已经在 protobuf 体系里，最顺：

* **protobuf event**（一条 event message）
* 外层封一层 `LogRecord`：

  * `len`（u16/u32）
  * `record_type`（event/snapshot）
  * `ciphertext`
  * `crc32`（可选）

日志内容建议 **AEAD(LogKey)** 加密，保证：

* 落地文件被拷走也看不懂
* 文件被篡改能检测出来

---

## 7) 复盘的输出是什么样？

### 最小可用复盘（MVP）

* 时间线：加入/离开/集合/求助/掉线/恢复
* 地图回放：按时间推进显示轨迹（不要求动画，先做“按时间跳点”）

### Leader 复盘增强

* 统计：队伍最大拉开距离、成员掉线次数、集合耗时
* 异常：谁长时间静止、谁电量低、何时丢包严重

---

## 8) 最重要的落地决策：你要不要“复盘开关”？

我强烈建议在建队时就决定：

* `Record mode: Off / Local / Leader`
* `Retention: 1 day / 7 days / manual delete`
* `Privacy: Keep logs encrypted (default)`

并且在解散时：

* 如果 record off → 删除日志 + 销毁 LogKey
* 如果 record on → 写入 `TEAM_DISBANDED` 事件 + 关闭记录，但保留 LogKey（本机）

---

## 9) 你问的“整理一下”：需要持久化的新增数据

在你之前 `PersistentTeamState` 基础上，加这几项就够：

**当 Record=ON 时：**

* `log_enabled`（bool）
* `log_mode`（enum: LOCAL / LEADER）
* `log_key`（bytes）✅（独立于 TeamKey）
* `log_file_id` / `log_path`（如果 SD）
* `log_seq`（用于事件递增编号）
* `last_snapshot_offset`（可选）

**当 Record=OFF 时：**

* 不持久化任何 log 相关内容

---

# 1) SD 文件布局

## 1.1 目录结构（按 Team 会话分桶）

建议每次 Team 生命周期（Create→Disband）生成一个 **session_id**（随机 64-bit 或时间戳+随机），作为一个会话目录。

```
/trail-mate/
  /team_logs/
    /YYYYMMDD/
      /T_<session_id>/
        meta.json
        keys.bin
        events.log
        snapshot.idx
        snapshots.bin
```

### 文件职责

* `meta.json`（明文、可读）

  * 仅放非敏感信息：创建时间、设备名、版本号、记录模式等
* `keys.bin`（加密/保护）

  * 存 LogKey 的封装（详见 3）
* `events.log`（核心追加写日志）

  * 事件流（加密记录）
* `snapshot.idx`（小索引，方便快速定位）

  * 每个快照对应 `events.log` 的 offset + `snapshots.bin` 的 offset
* `snapshots.bin`（快照流，追加写）

  * 复盘加速的状态快照（加密记录）

> **MUST**：`events.log` 与 `snapshots.bin` 都是 *append-only*（只追加、不改写、不截断）。
> **SHOULD**：每天分目录，方便清理/归档。

---

# 2) LogRecord 二进制格式（通用记录容器）

`events.log` 和 `snapshots.bin` 使用同一种 record 容器：`LogRecord`。
每条记录都是：**固定头 + 变长密文 + 可选 CRC**。

## 2.1 LogRecord 结构（小端序 Little-endian）

```
+-------------------------------+
| magic      (4)  "TLOG"        |
| version    (1)  = 1           |
| type       (1)  1=EVENT 2=SNAP|
| flags      (2)                |
| header_len (2)  bytes         |
| body_len   (4)  bytes         |
| session_id (8)                |
| seq         (8) monotonic     |
| ts_ms       (8) unix ms       |
| key_id      (4)               |
| nonce      (12)               |
| aad_crc32   (4)               |  (optional but recommended)
|--------------------------------
| AAD (header extension...)      |  (header_len - fixed_header bytes)
|--------------------------------
| ciphertext (body_len bytes)    |  AEAD output (may include tag)
|--------------------------------
| trailer_crc32 (4) optional     |
+-------------------------------+
```

### 固定头字段说明

* `magic`：用于快速扫描与恢复
* `version`：格式版本
* `type`：EVENT / SNAP
* `flags`：位标志（是否有 trailer_crc、是否压缩等）
* `header_len`：头部总长度（方便未来扩展 AAD）
* `body_len`：密文长度
* `session_id`：会话绑定（防止混写）
* `seq`：单调递增序号（掉电恢复、去重、对齐）
* `ts_ms`：记录写入时间（或事件发生时间，看你定义）
* `key_id`：LogKey 轮换版本（可选但强烈推荐）
* `nonce(12)`：AEAD nonce（每条记录唯一）
* `aad_crc32`：对 AAD 计算 CRC（用来快速判断 header 是否被破坏）

> **MUST**：`seq` 单调递增且不重复（同一 session 内）。
> **MUST**：`nonce` 在同一 `key_id` 下不可重复。
> **SHOULD**：`header_len` 允许携带扩展 AAD（比如 sender_id、event_type 等的明文字段）。

---

## 2.2 flags 位定义（建议）

* bit0: `HAS_TRAILER_CRC`（记录末尾带 `trailer_crc32`）
* bit1: `BODY_COMPRESSED`（密文内部的明文在加密前压缩）
* bit2: `TS_IS_EVENT_TIME`（ts_ms 表示事件发生时间，否则表示写入时间）
* bit3: `RESERVED`

> 推荐默认打开 `HAS_TRAILER_CRC`。即使 AEAD 能校验密文完整性，CRC 仍对**快速定位损坏/截断**很有价值。

---

# 3) 加密与密钥存放（LogKey）

## 3.1 LogKey 的定位

* TeamKey 用于 Team 通信（可能在 Disband 时销毁）
* **LogKey 用于日志加密**，是否保留由 “Record Mode/Retention Policy” 决定

> 你想要“可复盘”，就必须 **在 Disband 后仍可获得 LogKey**（至少在本机）。

## 3.2 keys.bin（推荐结构）

`keys.bin` 不要明文放 LogKey。建议用设备唯一密钥封装（例如 ESP32 NVS 加密分区 / eFuse key / 或你自己的 device key）。

`keys.bin` 内容建议也是一个小的 `KeyRecord`：

```
magic "TKEY" (4)
version (1)
flags   (1)
reserved(2)
session_id (8)
key_id (4)
nonce (12)
ciphertext (var) = AEAD(DeviceKey, LogKeyMaterial)
crc32 (4)
```

其中 `LogKeyMaterial` 至少包含：

* `log_key` (32 bytes)
* `kdf_info`/algo id（可选）
* `created_ts`（可选）

> **MUST**：设备没有解封 `keys.bin` 的能力时，不允许进入复盘（避免误显示）。

---

# 4) events.log 里明文到底放什么（EVENT 内容）

EVENT 的明文建议用 protobuf（或你自定义 TLV），然后 AEAD 加密。

### 明文建议结构：`TeamEvent`（protobuf）

包含：

* `event_type`
* `sender_id`
* `team_id`
* `payload`（按类型 oneof）
* `mesh_ts`（可选）
* `rx_rssi/snr`（可选）
* `seq_in_mesh`（可选）

> **SHOULD**：把 `event_type` 放到 LogRecord 的 AAD 扩展里（明文），这样你可以不解密就做快速过滤/统计；但会泄露“事件类别”——如果你非常在意隐私，就不要这么做，把一切放密文里。

---

# 5) CRC 策略（掉电与损坏恢复的关键）

你有三层完整性：

1. AEAD tag：保证密文不可篡改（但无法判断“文件截断”位置）
2. `aad_crc32`：快速判断 header 是否损坏
3. `trailer_crc32`：对整个 record（从 magic 到 ciphertext）做 CRC，便于恢复扫描

### trailer_crc32 计算范围（建议）

对 `LogRecord` 从 `magic` 开始到 `ciphertext` 末尾（不含 trailer_crc32 本身）做 CRC32。

> **MUST**：读取时若 CRC 不匹配，该 record 之后的数据可视为不可信，进入恢复扫描模式。

---

# 6) 追加写策略（append-only）

## 6.1 写入流程（每条 record）

1. 组装明文 event
2. 生成 nonce
3. AEAD 加密得到 ciphertext
4. 写入 LogRecord 头 + ciphertext
5. 写入 trailer_crc32（若启用）
6. `fsync/flush`（看你平台：至少在关键事件/周期性 flush）

### Flush 策略（SD 实用建议）

* 普通事件：可每 N 条或每 T 秒 flush 一次（例如 2s）
* 关键事件（TEAM_CREATED / DISBANDED / ROTATED / SOS）：**立即 flush**

> **SHOULD**：实现一个 `LogWriter`，内部有小缓冲；但不要大到掉电损失太多。

---

# 7) 快照策略（snapshot + index）

复盘如果只靠 replay events，会越来越慢。快照就是为了“从某个点开始回放”。

## 7.1 快照内容

快照明文建议存：

* 当前 `key_id`
* 成员表（最近可见成员、最后位置）
* 当前 waypoint/assemble point
* 关键参数（采样策略）
* `last_event_seq`（快照覆盖到的最后事件序号）

然后 AEAD(LogKey) 加密，写入 `snapshots.bin`（同 LogRecord 格式，type=SNAP）。

## 7.2 何时写快照（建议）

* 每 **N 条事件**（例如 200）
* 或每 **T 秒**（例如 60s）
* 或关键事件后（rotate/disband）

> **MUST**：快照写入不应影响实时通信，必须可丢弃（失败不影响主功能）。

## 7.3 snapshot.idx（小索引，明文）

索引是一条条固定长度记录，追加写，便于快速定位最新快照：

```
IdxEntry (fixed 32 bytes):
- magic "SIDX" (4)  (optional per file header)
- version (1)
- reserved(3)
- session_id (8)
- snap_seq (8)          // snapshot record seq
- events_offset (8)     // events.log offset at snapshot time
- snaps_offset (8)      // snapshots.bin offset for this snapshot
```

> **SHOULD**：`snapshot.idx` 可明文，因为它只暴露 offset；如果你认为“有无快照/频率”也敏感，可把 idx 也加密，但工程复杂度会上升。

---

# 8) 恢复与复盘读取流程（掉电后可自愈）

## 8.1 打开一个 session 的复盘步骤

1. 读取 `meta.json`（可选）
2. 解封 `keys.bin` 得到 LogKey
3. 读取 `snapshot.idx`，找到最后一个可用快照（校验 snap record CRC/AEAD）
4. 从快照恢复状态
5. 从 `events.log` 的 `events_offset` 开始，顺序读取 record：

   * magic 检查
   * header/aad_crc32 检查
   * trailer_crc32（若启用）
   * AEAD 解密
   * 解析 event → 回放更新状态

## 8.2 恢复扫描（文件损坏/截断）

当发现 CRC 不匹配或读到半条 record：

* 进入扫描模式：按字节滑动找下一个 `"TLOG"` magic
* 找到后尝试解析 header_len/body_len 是否合理
* CRC 通过则继续

> **MUST**：扫描必须有上限（避免在损坏文件上死循环）。

---

# 9) 文件滚动与保留策略（防止无限增长）

## 9.1 单 session 文件滚动

建议 `events.log` 达到阈值就切分：

* `events_0001.log`
* `events_0002.log`

同理快照。

阈值建议：

* 4MB / 16MB / 64MB 任选一个（看 SD/需求）
* 位置点多的话建议更小，便于导出与修复

## 9.2 Retention（保留期）

* `meta.json` 里记录 retention policy
* 定期清理 `YYYYMMDD` 目录

> **MUST**：用户应能“一键删除某次行动记录”（删除整个 `T_<session_id>` 目录）。

---

# 10) 最小实现版本（你可以先落地这个）

如果你要先快速跑起来，我建议 MVP：

* 只要三个文件：

  * `meta.json`
  * `keys.bin`
  * `events.log`
* `events.log`：LogRecord + AEAD + trailer_crc32
* 不做快照、不做 idx
* 位置事件做强制降采样（例如 10s 一条）

后续再加：

* `snapshots.bin` + `snapshot.idx`

---
