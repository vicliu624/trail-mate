# Pager Team Core｜SD-only 持久化方案（完整版）

## 0. 目标与三条原则

### 目标

* **简单**：文件少、格式少、逻辑少
* **可信**：断电可恢复、日志不会写坏全局
* **资源消耗低**：避免高频随机写；控制日志增长

### 核心原则

* **SD-only**（不磨损 flash）
* **append-only**（日志只追加，不回写不 seek）
* **无每条 CRC**：靠 `magic + len` 判定完整记录；**尾巴不完整直接丢弃**
* **“意图 vs 事实”分离**：UI 触发 **Intent**；落盘写入 **Committed Fact**

---

## 1. 目录结构（定稿）

```text
/team/
  current.txt                 # 当前队伍目录名，一行文本
  current.tmp                 # 原子写临时文件
  T_A7K3/                      # team_id 的短码/hash（稳定目录名）
    keys.bin                  # 当前 team_psk（可选，单独持久化）
    snapshot.bin              # 当前世界快照（低频原子写）
    snapshot.tmp              # 原子写临时文件
    events.log                # 关键事件日志（追加写，sync 依据）
    posring.log               # 位置 ring（固定大小，覆盖最旧）
    chatlog.log               # 聊天日志（滚动上限）
```

### current.txt 格式

* 内容示例：`T_A7K3\n`
* 原子写：写 `current.tmp` → flush → rename 覆盖 `current.txt`

---

## 2. 数据模型分层（必须坚持的边界）

### 2.1 关键事件（Key Events）

**改变团队结构 / epoch / waypoint 的事实**，必须：

* 写入 `events.log`
* 带 `event_seq`（单调递增）
* 作为 SYNC 的依据

**必须写入**的关键事件类型：

* `TeamCreated`
* `MemberAccepted`
* `MemberKicked`
* `LeaderTransferred`
* `EpochRotated`
  （Waypoint 可选，后续加同样走 key event）

### 2.2 高频态势（Non-key）

* Presence：心跳/在线状态（不落盘）
* Position：落 `posring.log`（ring + 节流）
* Chat：落 `chatlog.log`（滚动上限）

> 关键事件 = “世界规则变化”；位置/聊天 = “现场噪声与记录”。

---

## 3. event_seq：权威来源与落盘规则（答疑合并）

### 3.1 event_seq 谁维护？

✅ **Leader 维护并分配 event_seq**（推荐在 TeamCore/Usecase 层维护；TeamService 保持 IO 职责）

* Leader 产生 key event 时：`event_seq = last_event_seq + 1`
* Member 只接受带 seq 的 key event，并校验连续性

### 3.2 UI 触发还是接收后落盘？

✅ **两者都有，但落盘只能写“已提交事实（Committed Fact）”**

* UI 触发的是 **Intent**（踢人/转让/轮换等请求）
* 只有当 Leader / Member **确认提交**后，才写入 `events.log`

具体：

* **Leader**：UI Intent → 校验权限/状态 → Commit（分配 seq）→ append events.log → apply 内存状态 → 发送消息（失败靠后续 SYNC 修复）
* **Member**：收到 leader 的 key event → 校验 team_id/epoch/权限/seq 连续性 → append events.log → apply

> 避免出现“UI 点了但网络没发出去/没人承认，日志却写死了”的分裂。

---

## 4. 文件格式（team_id 统一 8 字节，无 CRC）

通用约定：Little-endian、紧凑写、不依赖 struct padding。

---

### 4.1 TeamId

* `team_id`：`uint64_t`（8 bytes）
* 目录名建议：`T_` + base32(team_id)（或短 hash），保证 FAT 兼容

---

### 4.2 snapshot.bin（当前世界快照）

#### Header（固定）

```c
SnapshotHeaderV1 {
  char   magic[4] = "TMS1";
  uint8  version  = 1;
  uint8  flags;                 // bit0: in_team
  uint16 reserved;

  uint32 updated_ts;            // 写快照时间（秒）
  uint64 team_id;               // 8B
  uint32 epoch;                 // 当前 epoch
  uint32 last_event_seq;        // 已应用到的最后 seq

  uint32 self_node_id;
  uint32 leader_node_id;

  uint8  self_role;             // 0 None, 1 Member, 2 Leader
  uint8  reserved2[3];

  uint16 member_count;
  uint16 reserved3;
}
```

#### 成员表（重复 member_count 次，变长）

```c
MemberRecV1 {
  uint32 node_id;
  uint8  role;                  // 1 Member, 2 Leader
  uint8  flags;                 // bit0: has_name
  uint16 name_len;              // 建议 <= 24
  char   name[name_len];        // UTF-8
}
```

#### 原子写

写 `snapshot.tmp` → flush → rename 覆盖 `snapshot.bin`

#### 写入节流（低消耗）

* 每 **10 条** key event 或 **60 秒**写一次（取先到）
* epoch rotate / kick / transfer / join 成功 / 离队：**强制写一次**

---

### 4.3 events.log（关键事件日志：真相源 + SYNC 依据）

#### Record header（固定）

```c
EventRecHeaderV1 {
  char   magic[2] = "EV";
  uint8  version  = 1;
  uint8  type;                  // KeyEventType

  uint32 event_seq;             // 单调递增
  uint32 ts;                    // 秒

  uint16 payload_len;
  uint16 reserved;
}
payload[payload_len]
```

#### 无 CRC 的可信规则

扫描时若：

* magic/version 不匹配 → 停止（最简单策略）
* 文件剩余长度 < header + payload_len → 停止（尾部半条丢弃）

因为只 append，停止点之前的数据可信。

#### KeyEventType

* `1 TeamCreated`
* `2 MemberAccepted`
* `3 MemberKicked`
* `4 LeaderTransferred`
* `5 EpochRotated`

#### Payload 定义

**TeamCreated**

```c
uint64 team_id
uint32 leader_node_id
uint32 epoch          // 初始 1
```

**MemberAccepted**

```c
uint32 member_node_id
uint8  role           // 默认 1 Member
uint8  reserved[3]
```

**MemberKicked**

```c
uint32 member_node_id
```

**LeaderTransferred**

```c
uint32 new_leader_node_id
```

**EpochRotated**

```c
uint32 new_epoch
```

> payload 尽量固定字段、少字符串：更省空间，更低 IO。

---

### 4.3.1 keys.bin（team_psk 独立持久化，避免重启后 keys 未就绪）

> snapshot 不保存密钥，密钥单独落盘。  
> 仅保存当前生效的 team_psk + key_id（epoch），不做历史。

#### 格式（固定）

```c
TeamKeysFileV1 {
  char   magic[4] = "TMK1";
  uint8  version  = 1;
  uint8  psk_len;              // 16
  uint16 reserved;

  uint64 team_id;
  uint32 key_id;               // epoch/key_id
  uint8  psk[16];              // team_psk
}
```

#### 原子写

写 `keys.tmp` → flush → rename 覆盖 `keys.bin`

---

### 4.4 posring.log（位置 ring，高频但节流）

#### Header（固定）

```c
PosRingHeaderV1 {
  char   magic[4] = "PSR1";
  uint8  version  = 1;
  uint8  reserved1[3];

  uint32 data_capacity;         // data 区大小（固定）
  uint32 write_offset;          // 下一条写入位置
  uint32 rec_size;              // 固定 = sizeof(PosRecV1)
  uint32 reserved2;
}
data[data_capacity]
```

#### Record（固定）

```c
PosRecV1 {
  uint16 magic = 0x5053;        // 'PS'
  uint8  ver   = 1;
  uint8  flags;

  uint32 ts;
  uint32 member_id;
  int32  lat_e7;
  int32  lon_e7;

  int16  alt_m;
  uint16 speed_dmps;
}
```

#### 写入节流（必须）

* 每成员 15–30 秒最多写一条，或位移 > 20m 才写
* ring 满了覆盖最旧，文件大小恒定

---

### 4.5 chatlog.log（聊天追加写 + 上限滚动）

#### Record（变长）

```c
ChatRecHeaderV1 {
  char   magic[2] = "CH";
  uint8  version  = 1;
  uint8  flags;                 // bit0 incoming/outgoing

  uint32 ts;
  uint32 peer_id;

  uint16 text_len;
  uint16 reserved;
}
text[text_len]                  // UTF-8
```

#### 上限策略（简单）

* 上限：**256KB** 或 **1000 条**（二选一）
* 超限：

  * rename `chatlog.log` → `chatlog.old`（可选）
  * 新建空 `chatlog.log`

---

## 5. 启动恢复流程（有 current.txt，最快）

1. 读 `/team/current.txt`

* 不存在/空 → 当前无队伍

2. 打开 `/team/<dir>/snapshot.bin`

* 若不存在 → 从空状态开始（但通常 join 后会写一次）

3. 增量回放 `events.log`

* 从 `snapshot.last_event_seq + 1` 开始顺序扫描应用
* 遇到尾部不完整 → 停止

4. UI Ready

---

## 6. SYNC 流程（依赖 event_seq）

* Presence（不落盘）携带：`team_id, epoch, last_event_seq`
* 发现对方 `last_event_seq > my_last_event_seq`：

  * 发送 `SYNC_REQ(from_seq = my_last_event_seq + 1)`
* 对方从 `events.log` 读 seq 范围（最多 N 条）回 `SYNC_RSP`
* 本地 apply + append（对已存在 seq 可跳过），更新 snapshot

---

## 7. event_seq + commit 流程（ASCII 状态/时序图）

### 7.1 Leader：UI Intent → Commit → Log → Broadcast

```text
UI                         TeamCore/Usecase                 TeamStore(SD)              TeamService(IO)           Mesh
 |   intentKick(target)          |                              |                           |                    |
 |------------------------------>|  check(role==Leader)         |                           |                    |
 |                               |  build KeyEvent(Kicked)      |                           |                    |
 |                               |  seq = last_seq + 1          |                           |                    |
 |                               |----------------------------->| append events.log(EV,seq)|                    |
 |                               |<-----------------------------| ok                        |                    |
 |                               |  apply to in-mem state        |                           |                    |
 |                               |  maybe snapshot throttle      |                           |                    |
 |                               |------------------------------>| save snapshot.tmp->bin    |                    |
 |                               |<-----------------------------| ok                        |                    |
 |                               |----------------------------------------------------------->| sendKick(seq,...)  |
 |                               |                                                            |------------------->|
 |                               |                                                            |   (may fail)       |
 |                               |  NOTE: even if send fails, state is committed;             |                    |
 |                               |        later Status/SYNC will repair delivery              |                    |
```

### 7.2 Member：RX KeyEvent → Verify seq → Log → Apply（缺 seq 触发 SYNC）

```text
Mesh                 TeamService(IO)             TeamCore/Usecase             TeamStore(SD)
 |  RX Kick(seq,...)       |                           |                         |
 |------------------------>| decode/decrypt            |                         |
 |                         | sink_.onTeamKick(...) ---->| verify team_id/epoch   |
 |                         |                           | verify seq == last+1 ? |
 |                         |                           |    NO -> request SYNC  |
 |                         |                           |    YES -> commit apply |
 |                         |                           |------------------------> append events.log
 |                         |                           |<------------------------ ok
 |                         |                           | apply in-mem state
 |                         |                           | maybe snapshot throttle
 |                         |                           |------------------------> save snapshot atomic
 |                         |                           |<------------------------ ok
```

### 7.3 seq 不连续时（Member 触发 SYNC）

```text
Member Core                          Mesh                         Leader Core
    |  see seq gap (expected=41 got=45)                             |
    |---------------- SYNC_REQ(from=41) --------------------------->|
    |                                                               | read events.log seq>=41
    |<---------------- SYNC_RSP(events 41..45) ---------------------|
    | apply + append + snapshot                                     |
```

---

## 8. 工程接入点（与 TeamService / ITeamEventSink 对齐）

你现在 `TeamService::processIncoming()` 会把消息解包后丢给 `sink_.onTeamXxx(event)`。

推荐最小改动：

* `sink_` 的实现（TeamCore/Usecase）负责：

  * 权限判断
  * seq 分配（leader）
  * seq 连续性检查（member）
  * 调用 `TeamStore.append_key_event(...)` 写 `events.log`
  * 更新内存状态
  * 触发 snapshot（节流）
* `TeamService` 仅做 IO：decode/encode/send，不直接写 SD

---

## 9. PosRing / ChatLog 接入点（答疑合并入规范）

### 9.1 posring.log：从接收写还是从发送写？

✅ 定稿：**两边都写，并统一走同一个节流器**

* **收到 TeamPosition（队友）**：写入 `posring.log`（队友态势来源）
* **本机 GPS fix（自己）**：也写入 `posring.log`（重启后仍能立即看到自己最后位置/轨迹片段）

原因：

* 只写接收 → 自己历史为空
* 只写发送 → 队友态势重启后为空
* posring 是“事实流缓存”，不参与一致性：不用 event_seq，不用严格去重
  只需做到：每成员节流 + ts 最新覆盖 UI

**推荐接入（最干净）**：只在 **TeamCore（sink 实现）**里写 posring

* `sink_.onTeamPosition(event)`：decode → `posring_append_throttled(from_id, pos, ts)`
* 本机 GPS 更新处：`TeamCore::onLocalPositionFix(fix)` → `posring_append_throttled(self_id, pos, ts)` → 再决定是否 `TeamService.sendPosition(...)`

### 9.2 chatlog.log：记录 Mesh chat（Team channel）还是仅 Team 协议？

先分清三种消息域：

1. **Meshtastic 普通聊天**（你现有 chat 模块）
2. **Team 管理消息（TEAM_MGMT_APP）**：Join/Kick/Rotate/Status…（不是聊天）
3. **队内聊天**（可复用 Meshtastic text，也可未来自定义 Team chat port）

✅ 定稿：**chatlog.log 只记录用户可见的“聊天文本”，不记录管理消息**

* ❌ 不记录 `TEAM_MGMT_APP` 到 chatlog（它们属于 `events.log` / diagnostics）
* ✅ 记录“队内聊天文本”

  * 推荐走 **路线 A**：记录 **Meshtastic text 中属于当前 Team channel 的消息**

路线 A（推荐）：记录 Meshtastic 文本聊天（Team channel）

* 优点：立刻兼容现有生态（安卓/ATAK/其他节点）
* 缺点：聊天与 Team 语义解耦（可接受）

路线 B（后续版本）：Team 专用 chat 协议（TEAM_CHAT_APP / TEAM_MGMT.Chat）

* 优点：聊天与 team_id/epoch 绑定、成员可控
* 缺点：需要新增协议与兼容处理

**chatlog 的接入点（工程）**

* 因为 TeamService 不处理 text chat，所以 chatlog 应在 **chat 模块**接入：

  * ChatService/ChatUsecase 收到/发送文本时：

    * 若 `channel_id == current_team_channel` → `chatlog_append(...)`
* TeamCore 只需要提供“当前 team channel_id”

---

## 10. 缺口补齐总结（当前约束）

* team_id：✅ 8 字节（uint64）
* 关键事件必须落盘：✅ events.log + type + payload 完整定义
* event_seq：✅ leader 权威维护；snapshot 记录 last_seq；member 检查连续性；缺口走 SYNC
* Intent vs Fact：✅ UI 触发 Intent；落盘只写 Committed Fact
* posring/chatlog：✅ 接入点与记录范围明确（pos 双写、chat 记 team channel 文本）

---

# Team Position（对齐 Meshtastic POSITION_APP）

## 1) 设计目标

* **对齐 Meshtastic**：payload 直接使用 `meshtastic_Position` protobuf
* **生态兼容**：与 Meshtastic POSITION_APP 解码一致
* **扩展性**：保留 Meshtastic 字段的自然扩展空间

---

## 2) Wire Payload

* **类型**：`meshtastic_Position`（protobuf）
* **端口**：`meshtastic_PortNum_POSITION_APP`
* **编码**：nanopb / pb_encode

> 说明：不再使用自定义布局。Position payload 与 Meshtastic 完全一致。

---

## 3) 取值规则（最小集）

* `latitude_i` / `longitude_i`：E7（int32）
* `location_source`：默认 `LOC_INTERNAL`
* `sats_in_view`：有则填
* `timestamp`：有有效 epoch 秒则填

其余字段按需填充，保持与 Meshtastic 语义一致。

---

# 5) 编码/解码接入点（对齐你现有 TeamService + sink）

## 5.1 发送端（本机 GPS 更新）

**推荐路径**（TeamCore 层做事）：

1. `TeamCore::onLocalPositionFix(fix)`
2. 生成 `meshtastic_Position` payload（protobuf）
3. **先** `posring_append_throttled(self_id, decoded_pos)`（同一个点落盘）
4. 再调用 `TeamService.sendPosition(payload, team_channel)`

> 你问过“posring 从发送写还是接收写”：这里就是“发送也写”。

## 5.2 接收端（TEAM_POSITION_APP）

你现在流程已经有：

* `TeamService::processIncoming()` 解密 → `sink_.onTeamPosition(event{ctx, plain})`

在 `sink` 的实现（TeamCore）里：

1. decode `meshtastic_Position` → 得到标准化字段
2. `posring_append_throttled(from_id, pos)`（队友点落盘）
3. 更新内存 `last_seen`（presence/online 状态）

---

# 6) posring.log 落盘字段对齐建议

你现有 `PosRecV1` 足够用（ts、member_id、lat/lon、alt、speed）。
`course` 和 `vbat`：

* **可以不落盘**（UI 大多用轨迹方向估计就够了；vbat 在 topbar 也不一定要显示队友）
* 需要时再出 `PosRecV2`（加 2 字节 course、2 字节 vbat，不难）

---

# 7) 版本与兼容规则

* payload 必须能解码为 `meshtastic_Position`，否则丢弃（或统计 error）
* 不做自定义版本字段检查（以 Meshtastic 协议为准）


