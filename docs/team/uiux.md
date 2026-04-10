# A. 全部页面一览（按状态完整覆盖）

> 设备：2.33-inch 横屏，分辨率 222 x 480  
> 约束：固定 TopBar（Back / Title / Battery）  
> 原则：近距离组队优先使用 `ESP-NOW`，不依赖 LoRa / NFC；配对只在对应页面启用，避免误触和持续耗电。

---

## A0. 全局 UI 骨架（复用）

```text
+------------------------------------------------+
| < Back          [ TITLE / CONTEXT ]        Bat |
+------------------------------------------------+
|                                                |
|                 CONTENT AREA                   |
|                                                |
+------------------------------------------------+
| [ Action 1 ]          [ Action 2 ]             |
+------------------------------------------------+
```

---

## A1. Team 状态入口页（未加入）

**Title：`Team`**

- 主文案：You are not in a team
- 说明：
  - No shared map
  - No team awareness
- 主动作：
  - `Create (ESP-NOW)`
  - `Join (ESP-NOW)`

---

## A2. Team 状态入口页（已加入）

**Title：`Team Status`**

- 展示字段：
  - Team name / Team ID
  - Role
  - Members / Online
  - Security / Epoch
  - Sync / Last event
- Team Health：
  - Leader online
  - Last update age
  - stale member count
- 主动作：
  - `View Team`
  - `Pair Member`
  - `Leave`

这是判断“队伍是否健康、是否同步完成”的总览页，不承担复杂管理职责。

---

## A3. Team Home（成员与结构）

**Title：`Team / Leader` 或 `Team / Member`**

- 展示字段：
  - Team / ID
  - Members / Online
  - Epoch
  - Sync status
- 列表内容：
  - 成员名
  - 在线状态
  - 最近在线时间
- 主动作：
  - `Pair Member`
  - `Manage`（Leader 可见）
  - `Leave`

说明：户外场景里用户更常停留在地图页或聊天页，因此关键状态不能只靠弹窗提示。

---

## A3b. Pairing（ESP-NOW）

**Title：`Pairing`**

- 配对窗口有效期：120 秒
- Leader 侧：
  - 广播可加入状态
  - 接受加入请求
  - 下发密钥和初始快照
- Member 侧：
  - 扫描 beacon
  - 发送 join 请求
  - 等待 Key Distribution
- UI 状态：
  - `Scanning`
  - `Join sent`
  - `Waiting for keys`
  - `Completed`
  - `Failed`
- 动作：
  - `Cancel`
  - `Retry`

---

## A7. Members 管理页（Leader）

**Title：`Members`**

- 列表内容：
  - You (Leader)
  - 普通成员
  - 每个成员带 `Select`
- 用途：进入单成员详情，发起踢人或转移 Leader。

---

## A8. Member Detail（Leader）

**Title：`Member: <name>`**

- 展示：
  - Status
  - Role
  - Device
  - Capability（Position / Waypoint 等）
- 动作：
  - `Kick`
  - `Transfer Leader`

---

## A9. Kick 确认页

**Title：`Kick Member`**

- 文案：
  - Remove `<member>` from team?
  - This will update the security round (`epoch`).
  - The removed member will no longer receive team updates.
- 动作：
  - `Cancel`
  - `Confirm Kick`

---

## A9b. Leave 确认页

**Title：`Leave team?`**

- 文案：
  - This clears local keys.
- 动作：
  - `Cancel`
  - `Leave`

`Leave` 需要二次确认，避免误触导致本地密钥被清空。

---

## A10. Access Lost（Member：被移除 / 失步 / 异常）

**Title：`Team`**

- 状态：Access lost
- 原因：
  - `Revoked`
  - `Out-of-sync`
  - `Unknown`
- 说明：
  - Revoked：被 Leader 移除
  - Out-of-sync：队伍已经更新，本机需要同步
- 动作：
  - `Try Sync`
  - `Join Another Team`
  - `OK`

关键点：要明确区分“被踢出”和“epoch 不一致”，减少误判。

---

# B. 页面流转说明（UI 状态机）

## B1. 顶层流转

```text
[ Team Menu ]
     |
     v
[ Team Status ]
     |
     +--> (not in team) --> [ Create / Join ] -> [ Team Status (joined) ]
     |
     +--> (joined) -------> [ View Team ] -> [ Team Home ]
```

---

## B2. Member 加入流程（ESP-NOW）

```text
[ Team Status (not in team) ]
        |
        v
     [ Pairing ]
        |
        +-- scanning -> join sent -> waiting key -> [ Team Status (joined) ]
        |
        +-- timeout / cancel --------------------> [ Team Status (not in team) ]
```

---

## B3. Leader 配对流程（ESP-NOW）

```text
[ Team Status (leader) ]
     |
     v
  [ Pairing ]
     |
     +-- member joins -> send keys -> [ Team Status ]
     |
     +-- timeout / cancel ----------> [ Team Status ]
```

---

## B4. 踢人流程（Leader）

```text
[ Team Home ]
     |
     v
[ Members ]
     |
     v
[ Member Detail ]
     |
     v
[ Kick Confirm ]
     |
     +-- confirm --> epoch rotate --> [ Team Status ]
```

---

# C. 涉及的协议（Pager Team Core v0.1）

## C1. 主要消息类型

| 类型 | 说明 |
| --- | --- |
| `TEAM_KEY_DIST` | 通过 `ESP-NOW` 分发队伍密钥 |
| `TEAM_KICK` | 移除成员 |
| `TEAM_TRANSFER_LEADER` | 转移队长 |
| `TEAM_STATUS` | 队伍状态广播 |
| `TEAM_POS` | 成员位置同步 |
| `TEAM_WAYPOINT` | 队伍航点 |
| `TEAM_TRACK` | 轨迹数据 |
| `TEAM_CHAT` | 队伍聊天 |

`v0.2` 再考虑把部分消息扩展到 LoRa，`v0.1` 先把 Join Handshake 和本地状态闭环跑通。

---

## C2. 字段命名约定：`epoch` / `event_seq` / `msg_id`

- `epoch`：密钥轮次，用于选择当前有效 key。
- `event_seq`：关键事件序号，只用于关键事件同步，单调递增。
- `msg_id`：普通消息去重标识，可选；`v0.1` 可以先不做。

---

## C3. `TeamEnvelope`

所有 Team 消息统一包在一个外层结构里：

```text
TeamEnvelope {
  team_id
  epoch
  type
  sender_id
  timestamp
  msg_id?      // optional
  auth         // AEAD tag or MAC
  payload
}
```

说明：
- `event_seq` 只在关键事件或同步承载关键事件时出现。
- Presence / Position 这类消息不要求连续 `seq`。

---

## C4. Key Events（写入 `events.log` 的事实源）

`v0.1` 必须记录的关键事件：

- `TeamCreated(event_seq=1)`
- `MemberAccepted(event_seq++)`
- `MemberKicked(event_seq++)`
- `LeaderTransferred(event_seq++)`
- `EpochRotated(event_seq++)`

`Key Events` 是同步和恢复的依据；Presence / Position / Chat 不属于关键事件。

---

# D. 协议与 UI 的对应关系

## D1. Create（ESP-NOW）

- 本地生成 `team_id`
- 初始化 `epoch = 1`
- 写入本地快照
- 进入 `Team Status`

## D2. Join（ESP-NOW）

- 扫描 Leader 广播
- 发送 Join 请求
- 等待 `TEAM_KEY_DIST`
- 写入初始快照与密钥
- 进入 `Team Status`

## D3. Kick / Leave / Transfer Leader

- 这些都是关键事件
- 必须更新 `event_seq`
- 必要时轮换 `epoch`
- UI 成功态统一回到 `Team Status`
