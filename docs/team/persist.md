# 0. 总体约定（所有二进制文件通用）

* **字节序**：小端（little-endian）
* **对齐**：按字节紧凑写入（不要依赖 `struct` 直接 fwrite，避免 padding）
* **版本**：每个文件都有 `version` 字段，未来升级靠 version 分支解析
* **可信性来源**（无 CRC）：

  * `magic` 判定记录边界
  * `len` 判定记录完整性
  * 启动/扫描时：**遇到不完整尾巴直接停止**（丢最后半条）

---

# 1) `/team/current.txt`（当前队伍指针文件）

## 格式

纯 ASCII 文本，一行：

```
T_A7K3\n
```

* `T_` + `[A-Z2-7]{4..10}`（推荐 base32/短 hash）
* 允许空文件表示“当前无队伍”

## 原子写规则

* 写 `/team/current.tmp`
* flush
* rename 覆盖 `/team/current.txt`

---

# 2) `/team/T_xxxx/snapshot.bin`（当前世界快照，低频原子写）

> snapshot 是“开机即用”的状态，**不是历史**。历史在 events.log。

## 2.1 Snapshot 文件头（固定长度）

```
SnapshotHeaderV1 {
  char   magic[4]   = "TMS1";     // Team Snapshot v1
  uint8  version    = 1;
  uint8  flags;                   // bit0: in_team
  uint16 reserved;

  uint32 updated_ts;              // 写 snapshot 的时间（秒）
  uint32 epoch;
  uint32 last_event_seq;

  uint32 self_node_id;
  uint32 leader_node_id;

  uint8  self_role;               // 0 None, 1 Member, 2 Leader
  uint8  reserved2[3];

  uint8  team_id[16];             // 128-bit
  uint16 member_count;
  uint16 waypoint_count;          // v0.1 可为 0（先预留）
}
```

> 你也可以不存 waypoint_count（用 TLV），但这个更简单更快。

## 2.2 成员表（member_count 个，固定长度记录）

```
MemberRecV1 {
  uint32 node_id;
  uint8  role;            // 1 Member, 2 Leader
  uint8  flags;           // bit0: has_name
  uint16 name_len;        // 0..24（建议限制，避免长字符串）
  char   name[name_len];  // UTF-8，无 \0
}
```

> 资源消耗低的做法：name_len 固定上限，比如 24；或者直接不存 name，仅 node_id+role。

## 2.3 Waypoint（v0.1 可暂不实现，但格式先给你）

Waypoint 是对象，需要一致性。建议 **LWW（Last-Write-Wins）** 最小字段：

```
WaypointRecV1 {
  uint8  object_id[8];    // 64-bit 随机或 hash
  uint32 version;         // 单调递增或时间戳版
  uint32 updated_ts;
  int32  lat_e7;
  int32  lon_e7;
  uint8  flags;           // bit0: deleted
  uint8  name_len;        // 0..32
  char   name[name_len];  // UTF-8
}
```

## 2.4 原子写

* 写 `snapshot.tmp`
* flush
* rename → `snapshot.bin`

---

# 3) `/team/T_xxxx/events.log`（关键事件日志，追加写，可 sync）

> **真正的“团队真相源”**（结构、epoch、waypoint 的变更都在这里）
> 无 CRC，用长度判定完整记录。

## 3.1 记录头（固定长度）

```
EventRecHeaderV1 {
  char   magic[2]   = "EV";   // Event record
  uint8  version    = 1;
  uint8  type;               // TeamEventType

  uint32 event_seq;          // 单调递增（leader 权威）
  uint32 ts;                 // 秒

  uint16 payload_len;        // payload 字节数
  uint16 reserved;
}
payload[payload_len]
```

### 可信性规则（无 CRC）

读取时：

* magic/version 不对 → 停止（或尝试找下一个 magic，v0.1 建议直接停止更简单）
* 文件剩余长度 < header + payload_len → **停止**（尾巴残缺丢弃）

## 3.2 TeamEventType（v0.1 最小集）

建议枚举值固定（便于兼容）：

* `1 TeamCreated`
* `2 MemberAccepted`
* `3 MemberKicked`
* `4 LeaderTransferred`
* `5 EpochRotated`
* `10 WaypointPut`
* `11 WaypointDel`
* `12 WaypointUpdate`（可选，Put 也能覆盖）

## 3.3 各事件 payload（尽量固定字段、少字符串）

### TeamCreated

```
payload:
  uint8  team_id[16]
  uint32 leader_node_id
  uint32 epoch   // 通常 1
```

### MemberAccepted

```
payload:
  uint32 member_node_id
  uint8  role    // 默认 Member
  uint8  reserved[3]
  // 可选 name：为了简单 v0.1 不带 name（name 走 UI 或后续事件）
```

### MemberKicked

```
payload:
  uint32 member_node_id
```

### LeaderTransferred

```
payload:
  uint32 new_leader_node_id
```

### EpochRotated

```
payload:
  uint32 new_epoch
```

### WaypointPut/Update

```
payload:
  uint8  object_id[8]
  uint32 version
  uint32 updated_ts
  int32  lat_e7
  int32  lon_e7
  uint8  flags   // bit0 deleted=0
  uint8  name_len
  char   name[name_len]
```

### WaypointDel

```
payload:
  uint8  object_id[8]
  uint32 version
  uint32 updated_ts
  uint8  flags   // bit0 deleted=1
```

## 3.4 写入规则（append-only）

* 每发生关键事件：写 `header` → 写 `payload` → flush
* 不回写、不 seek、不“补写尾巴”

---

# 4) `/team/T_xxxx/posring.log`（Position 固定大小 ring，高频但控写）

> Position 是“事实流”，不要求一致性，只要最近窗口够用。
> 采用 ring：**文件大小恒定，资源消耗可控**。

## 4.1 文件头（固定长度）

```
PosRingHeaderV1 {
  char   magic[4]   = "PSR1";
  uint8  version    = 1;
  uint8  reserved1[3];

  uint32 data_capacity;     // data 区字节数（固定）
  uint32 write_offset;      // 0..data_capacity-REC_SIZE，指向下一条写入位置

  uint32 rec_size;          // 固定 = sizeof(PosRecV1)=28/32
  uint32 reserved2;
}
data[data_capacity]
```

## 4.2 位置记录（固定长度，建议 28 bytes）

```
PosRecV1 {
  uint16 magic     = 0x5053;  // 'PS' (可选，强烈建议保留，便于扫描)
  uint8  ver       = 1;
  uint8  flags;              // bit0 has_fix

  uint32 ts;
  uint32 member_id;

  int32  lat_e7;
  int32  lon_e7;

  int16  alt_m;              // 可为 0
  uint16 speed_dmps;         // 0.1 m/s，可为 0
}
```

## 4.3 写入节流（必须，否则“低消耗”不成立）

* 每成员 **15–30 秒最多写一条**，或位移 > 20m 才写
* 这是你保持 SD IO 很低的关键

---

# 5) `/team/T_xxxx/chatlog.log`（Chat 追加写 + 上限滚动）

> chat 要回看，所以不做 ring（ring 也能做，但读“最后 N 条”更麻烦）。
> 用“变长记录 + 上限滚动”最简单。

## 5.1 记录格式（无 CRC，靠 length）

```
ChatRecHeaderV1 {
  char   magic[2] = "CH";
  uint8  version  = 1;
  uint8  flags;            // bit0 incoming(1)/outgoing(0)

  uint32 ts;
  uint32 peer_id;          // 对方 node_id（或 from_id）

  uint16 text_len;         // UTF-8 字节数
  uint16 reserved;
}
text[text_len]
```

## 5.2 文件上限与滚动

* 上限：例如 **256KB** 或 **1000 条**
* 超限策略（最简单）：

  * rename `chatlog.log` → `chatlog.old`（可选保留上一段）
  * 新建空 `chatlog.log`

---

# 6) 启动恢复与一致性（有了格式后，流程也明确）

## 6.1 启动

1. 读 `/team/current.txt` → 得到 `T_xxxx`
2. 读 `snapshot.bin`（如果没有就从空状态开始）
3. 从 `events.log` 里顺序扫描并回放：

   * 只回放 `event_seq > snapshot.last_event_seq`
   * 遇到尾巴不完整 → 停止
4. UI Ready

## 6.2 SYNC（补齐）

* presence 包带 `last_event_seq`
* 你落后 → 发 `SYNC_REQ(from_seq)`
* 对方从 `events.log` 扫描并打包返回最多 N 条事件（比如 50）
* 你 apply + append（可选择不重复写已存在 seq），然后更新 snapshot

---

# 7) 你可以直接照着实现的最小读写函数

* `write_current_atomic(dir_name)`
* `snapshot_save_atomic(state)`
* `events_append(type, seq, ts, payload)`
* `events_scan_apply(from_seq)`
* `posring_append_throttled(member_id, lat_e7, lon_e7, ts)`
* `chat_append(ts, peer_id, flags, text_utf8)`