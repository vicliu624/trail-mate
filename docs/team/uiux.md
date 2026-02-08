# A. 全部页面一览（按状态完整覆盖）

> 设备：**2.33-inch 横屏 222×480**
> 约束：**固定 TopBar（Back / Title / Battery）**
> 原则：**ESP‑NOW 近距建队（≤5m）**；不使用 LoRa/NFC；配对仅在对应页面启用（省电/避免误触）

---

## A0. 全局 UI 骨架（复用）

```
┌──────────────────────────────────────────────┐
│ < Back          [ TITLE / CONTEXT ]        🔋 │
├──────────────────────────────────────────────┤
│                                              │
│                CONTENT AREA                  │
│                                              │
├──────────────────────────────────────────────┤
│   [ Action 1 ]        [ Action 2 ]            │  (可选)
└──────────────────────────────────────────────┘
```

---

## A1. Team 状态入口页（未加入）

**Title：`Team`**

```
┌──────────────────────────────────────────────┐
│ < Back                 Team               🔋 │
├──────────────────────────────────────────────┤
│                                              │
│        You are not in a team                  │
│                                              │
│   • No shared map                             │
│   • No team awareness                         │
│                                              │
│   Create or join a team                       │
│                                              │
├──────────────────────────────────────────────┤
│  [ Create (ESP?NOW) ]      [ Join (ESP?NOW) ]           │
└──────────────────────────────────────────────┘
```

---

## A2. Team 状态入口页（已加入）

**Title：`Team Status`**

```
┌──────────────────────────────────────────────┐
│ < Back            Team Status             🔋 │
├──────────────────────────────────────────────┤
│  Team: ALPHA-7   (ID: A7K3)                   │
│  Role:        Member                          │
│  Members:     5   Online: 3                   │
│  Security:    OK   (Epoch 5)                  │
│  Sync:        OK   (Last event 128)           │
│                                              │
├──────────────────────────────────────────────┤
│  Team Health                                  │
│  ──────────────────────────────────────────  │
│  ● Leader online                              │
│  ● Last update 18s ago                        │
│  ○ 1 member stale                             │
│                                              │
├──────────────────────────────────────────────┤
│  [ View Team ]     [ Pair Member ]     [ Leave ]  │
└──────────────────────────────────────────────┘
```

> 这是 **判断“队伍是否还可信 / 是否对齐”** 的页面
> 不是管理页

---

## A3. Team Home（成员与结构，Leader/Member 通用）

**Title：`Team · Leader` 或 `Team · Member`**

```
┌──────────────────────────────────────────────┐
│ < Back          Team · Leader              🔋 │
├──────────────────────────────────────────────┤
│  Team: ALPHA-7 (ID: A7K3)                     │
│  Members: 3      Online: 2                    │
│  Epoch: 5        Sync: OK (128)               │
│                                              │
├──────────────────────────────────────────────┤
│                   │  (仅 Leader 且有请求时显示)
│  ──────────────────────────────────────────  │
│  Members                                     │
│  ● You (Leader)        Online                 │
│  ● Tom                 Online                 │
│  ○ Jerry               Last seen 2m ago       │
│                                              │
├──────────────────────────────────────────────┤
│  [ Pair Member ]        [ Manage ]     [ Leave ]  │
└──────────────────────────────────────────────┘
```

> 重要：户外场景下用户经常在地图/聊天页，不一定看得到 popup

---

## A3b. Pairing?ESP?NOW?
**Title?`Pairing`**

- ??????? ESP?NOW ??????? 120s?
- Leader?????????????????
- Member??? beacon??? join ????? KeyDist
- UI ???Scanning / Join sent / Waiting for keys / Completed / Failed
- ???[Cancel] ?????[Retry] ????

---

## A7. Members 管理页（Leader）

**Title：`Members`**

```
┌──────────────────────────────────────────────┐
│ < Back             Members               🔋 │
├──────────────────────────────────────────────┤
│  ● You (Leader)                               │
│                                              │
│  ● Tom                                       │
│     > Select                                  │
│                                              │
│  ○ Jerry                                     │
│     > Select                                  │
│                                              │
└──────────────────────────────────────────────┘
```

---

## A8. Member Detail（Leader）

**Title：`Member: Jerry`**

```
┌──────────────────────────────────────────────┐
│ < Back          Member: Jerry             🔋 │
├──────────────────────────────────────────────┤
│  Status:   Last seen 2m ago                   │
│  Role:     Member                             │
│                                              │
│  Device:   Pager                              │
│  Capability:                                  │
│   • Position                                  │
│   • Waypoint                                  │
│                                              │
├──────────────────────────────────────────────┤
│  [ Kick ]        [ Transfer Leader ]          │
└──────────────────────────────────────────────┘
```

---

## A9. Kick 确认页（安全关键）

**Title：`Kick Member`**

```
┌──────────────────────────────────────────────┐
│ < Back             Kick Member            🔋 │
├──────────────────────────────────────────────┤
│  Remove Jerry from team?                      │
│                                              │
│  This will update the security round (epoch). │
│  Jerry will no longer receive team updates.   │
│                                              │
├──────────────────────────────────────────────┤
│  [ Cancel ]              [ Confirm Kick ]     │
└──────────────────────────────────────────────┘
```

---

## A9b. Leave 确认弹窗

```
┌──────────────────────────────────────────────┐
│ < Back            Leave team?            🔋 │
├──────────────────────────────────────────────┤
│  This clears local keys.                     │
│                                              │
├──────────────────────────────────────────────┤
│  [ Cancel ]              [ Leave ]           │
└──────────────────────────────────────────────┘
```

> Leave 需要二次确认，避免误触导致本地密钥被清空

---

## A10. Access Lost（Member：被踢 / 失效 / 不一致）

**Title：`Team`**

```
┌──────────────────────────────────────────────┐
│ < Back               Team                🔋 │
├──────────────────────────────────────────────┤
│  Access lost                                  │
│                                              │
│  Reason:  [ Revoked | Out-of-sync | Unknown ] │
│                                              │
│  • Revoked: removed by leader                 │
│  • Out-of-sync: team updated, sync required   │
│                                              │
├──────────────────────────────────────────────┤
│  [ Try Sync ]   [ Join Another Team ]   [ OK ]│
└──────────────────────────────────────────────┘
```

> 关键：区分“被踢” vs “密钥/epoch 不一致”，减少误判
> `Try Sync` 只在 Out-of-sync 时启用

---

# B. 页面流转说明（UI 状态机）

## B1. 顶层流转（简化）

```
[ Team Menu ]
     |
     v
[ Team Status ]
     |
     +--> (not in team) --> [ Create (ESP?NOW) ] -> [ Team Status (joined) ]
     |
     +--> (joined) -------> [ View Team ] -> [ Team Home ]
```

---

## B2. ?????Member?ESP?NOW?
```
[ Team Status (not in team) ]
        |
        v
     [ Pairing ]
        |
        +-- scanning -> join sent -> waiting key -> [ Team Status (joined) ]
        |
        +-- timeout/cancel ----------------------> [ Team Status (not in team) ]
```

---

## B3. ?????Leader?ESP?NOW?
```
[ Team Status (leader) ]
     |
     v
  [ Pairing ]
     |
     +-- member joins -> send keys -> [ Team Status ]
     |
     +-- timeout/cancel --------------> [ Team Status ]
```

---

## B4. 踢人流程（Leader）

```
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

## C1. ???????????
| ?? | ?? |
| --- | --- |
| `TEAM_KEY_DIST` | ??????? ESP?NOW? |
| `TEAM_KICK` | ?????????? |
| `TEAM_TRANSFER_LEADER` | ?????????? |
| `TEAM_STATUS` | ?????????? |
| `TEAM_POS` | ???????? |
| `TEAM_WAYPOINT` | ???????? |
| `TEAM_TRACK` | ???????? |
| `TEAM_CHAT` | ?????????? |

> v0.2 ?????/???? ESP?NOW ??????LoRa ???? Join Handshake?
---

## C2. 字段命名定稿：epoch / event_seq / msg_id

为了避免实现踩雷，明确：

* `epoch`：**密钥轮次**（加解密 key 选择）
* `event_seq`：**关键事件序号**（仅 Key Events 与 Sync 使用，单调递增）
* `msg_id`：**普通包去重标识**（可选，v0.1 可不做）

---

## C3. TeamEnvelope（所有 Team 包统一外壳，v0.1）

> `event_seq` 只在关键事件或 sync 承载事件时出现；
> Presence/Pos 不要求连续 seq。

```
TeamEnvelope {
  team_id
  epoch
  type
  sender_id
  timestamp
  msg_id?          // optional, v0.1 may omit
  auth             // AEAD tag or MAC (depends on encrypt/plain)
  payload
}
```

---

## C4. Key Events（写入 events.log 的“结构真相”）

v0.1 必须记录的关键事件：

* `TeamCreated(event_seq=1)`
* `MemberAccepted(event_seq++)`
* `MemberKicked(event_seq++)`
* `LeaderTransferred(event_seq++)`
* `EpochRotated(event_seq++)`

> Key Events 是 Sync 的依据；Presence/Pos/Chat 不属于 Key Events。

---

# D. 协议流转（和 UI 的对应关系）

## D1. Create (ESP?NOW)

* 本地生成 `team_id`
* `epoch = 1`
* self = leader
* 生成 `team_key(epoch=1)`
* 追加 key event：`TeamCreated(event_seq=1)`
* 写 snapshot + events.log

---

## D2. Pairing / Join?ESP?NOW?

**Beacon?Leader?**

- Leader ?? Pairing ??????? Beacon
- Beacon ?? team_id / key_id / leader_id / team_name????

**Join & KeyDist?Member?**

1. Member ?? Beacon ??? Join ??
2. Leader ???? ESP?NOW ?? TEAM_KEY_DIST
3. Member ?? key ??? Joined??? LoRa ??? Team ??

---

## E2. 并发 Join 处理策略（Leader）

* Leader 同时只处理 **1 个 pending join**

\r\n
# 1) 模块清单（工程内目录与职责）

## 1.1 建议目录结构

```
src/team/
  domain/
    team_types.h
    team_model.h/.cpp
    team_events.h
    team_policy.h
    team_crypto.h
    team_codec.h         // Envelope 编解码（纯函数）

  usecase/
    team_service.h/.cpp  // 入口编排：UI动作 + Radio包 + 存储
    team_pairing_service.h/.cpp
    team_admin_flow.h/.cpp
    team_sync_flow.h/.cpp
    team_presence_flow.h/.cpp

  ports/
    i_team_store.h       // 持久化：snapshot + event log
    i_team_transport.h   // 发送/接收 Team 包（基于 Meshtastic portnum）
    i_team_crypto.h      // 生成/派生/加解密/MAC（可在 domain 里做，但建议 port）
    i_clock.h            // 时间戳（可复用系统已有）
    i_rng.h              // 随机数（team_id / key）
    i_ui_notifier.h      // 弹窗/Toast/页面跳转事件（可选）

  infra/
    meshtastic/
      mt_team_transport.h/.cpp   // 绑定 IMeshAdapter / portnum
    store/
      team_store_flash.h/.cpp    // Preferences/Flash
      team_store_sd.h/.cpp       // 可选：SD eventlog
    crypto/
      team_crypto_impl.h/.cpp    // AES/ChaCha + HMAC 等

  ui/
    screens/team/
      team_state.h/.cpp          // UI state：当前页、列表数据、loading/err
      team_pages.h/.cpp          // enter/exit, render
      team_input.h/.cpp          // 按键/旋钮映射为 action
      team_components.h/.cpp     // list item, modal, bottom actions
      team_nav.h/.cpp            // 简单导航（Status/Home/Pairing...）
```

> 你如果已经有 “screens/xxx/ layout/components/input/state” 的范式，就完全按那套套进去。

---

## 1.2 关键职责边界

### Domain（纯逻辑）

* **TeamModel**：只做状态机 + 事件应用（apply event）
* **TeamEventLog**：事件序号、重放、去重（只定义结构，不做 IO）
* **Policy**：超时阈值、invite TTL、presence 周期等（UI/配置层可改）
* **Codec**：TeamEnvelope + payload 的序列化/反序列化（不碰 radio）

> Domain 绝不直接“发包/存盘/弹 UI”。

### Usecase（编排）

* 接收 UI action → 调 domain → 调 ports（transport/store/crypto）
* 接收 radio packet → decode → 验证/解密 → apply → 必要时 sync/回包
* 输出 UI 需要的派生数据：team health、member list、security 状态

### Ports/Infra

* transport：把 “TeamPacket bytes” 发到 Meshtastic
* store：eventlog append、snapshot load/save
* crypto：key 派生、encrypt/decrypt、MAC 校验
* clock/rng：提供可测替身（单测、仿真）

### UI

* 页面只关心 **显示状态** + **发 action**
* UI 不做 join/kick 的业务判断（比如 epoch rotate 必须由 usecase 做）

---

# 2) 领域模型（Domain）最小定义

## 2.1 Team 状态（只包含 v0.1 必需）

```cpp
enum class TeamRole : uint8_t { Leader, Member, None };

enum class TeamSecurityState : uint8_t {
  OK,        // epoch匹配且可解密
  WARN,      // 需要sync或发现epoch差异但尚能工作
  FAIL       // 解密失败/被踢/密钥缺失
};

struct TeamId { std::array<uint8_t,16> bytes; };
struct MemberId { uint32_t node_id; };

struct MemberInfo {
  MemberId id;
  std::string name;        // 可选
  TeamRole role;           // Leader/Member
  uint32_t last_seen_ts;   // 秒
  uint32_t caps;           // 位标志：pos/wp/sync
};

struct TeamState {
  bool in_team = false;
  TeamId team_id;
  uint32_t epoch = 0;
  TeamRole self_role = TeamRole::None;
  MemberId leader_id;

  // 关键：成员集合（以事件重放得到）
  std::vector<MemberInfo> members;

  // 事件序号（用于sync）
  uint32_t last_event_seq = 0;

  // 安全态
  TeamSecurityState security = TeamSecurityState::FAIL;
};
```

## 2.2 关键事件（可重放）

```cpp
enum class TeamEventType : uint8_t {
  TeamCreated,
  MemberAccepted,
  MemberKicked,
  LeaderTransferred,
  EpochRotated,
  // v0.1 不做：复杂权限/对象
};

struct TeamEvent {
  uint32_t event_seq;      // 单调递增（leader为权威源）
  uint32_t ts;
  TeamEventType type;
  // payload: member_id, new_leader, new_epoch ...
};
```

## 2.3 TeamModel：唯一的“真相更新入口”

```cpp
class TeamModel {
public:
  TeamState s;

  // apply must be pure: no IO
  void apply(const TeamEvent& e);

  // derived
  bool isLeader() const;
  bool isMember() const;
  const MemberInfo* findMember(MemberId id) const;
};
```

---

# 3) Usecase 编排（核心 service + flows）

## 3.1 TeamService / PairingService

- TeamService ?? LoRa ?? Team ???status/pos/track/chat?
- TeamPairingService ?? ESP?NOW ?????Leader beacon / Member join / KeyDist?
- UI ?????Create (ESP?NOW) / Start Pairing / Stop Pairing / Leave / Kick / Transfer Leader
- Pairing ?? Pairing ????

---

# 4) 状态机伪代码（UI 与协议都在这里闭环）

下面给两套状态机：

* **UI 页面状态机**（你 LVGL 页面的流转）
* **协议/业务状态机**（join/kick/rotate/sync 的真实逻辑）

---

## 4.1 UI 页面状态机（Team Screen Navigation）

### UI 状态

```cpp
enum class TeamPage {
  StatusNotInTeam,
  StatusInTeam,
  TeamHome,
  JoinPending, // Pairing page
  Members,
  MemberDetail,
  KickConfirm,
  KickedOut
};

struct TeamUiState {
  TeamPage page;
  // selections
  int selected_member_index = -1;
  MemberId selected_member;
  // join temp
  std::string join_code;
  // flags
  bool busy = false;
  std::string toast;
};
```

### 页面流转伪代码（事件驱动）

```cpp
// called when entering Team menu
void TeamUI::enter() {
  auto s = teamService.snapshot();
  if (!s.in_team) navTo(StatusNotInTeam);
  else navTo(StatusInTeam);
}

// StatusNotInTeam
onClick(CreateTeam) { teamService.uiCreateTeam(); pairing.startLeader(); navTo(JoinPending); }
onClick(JoinTeam)   { pairing.startMember(); navTo(JoinPending); }

// StatusInTeam
onClick(ViewTeam)    { navTo(TeamHome); }
onClick(PairMember) { pairing.startLeader(); navTo(JoinPending); }
onClick(Leave)       { teamService.uiLeaveTeam(); navTo(StatusNotInTeam); }

// TeamHome
onClick(PairMember) { pairing.startLeader(); navTo(JoinPending); }
onClick(Manage)     { if (isLeader) navTo(Members); }
onClick(Leave)      { ... }

// JoinPending (Pairing)
onClick(Cancel) { pairing.stop(); navTo(previous); }
onClick(Retry)  { pairing.restart(); }

// Members
onSelect(Member) { ui.selected_member=...; navTo(MemberDetail); }

// MemberDetail
onClick(Kick) { navTo(KickConfirm); }
onClick(TransferLeader) { teamService.uiTransferLeader(ui.selected_member); navTo(TeamHome); }

// KickConfirm
onClick(ConfirmKick) { teamService.uiKickMember(ui.selected_member); navTo(StatusInTeam); }

// KickedOut
onClick(JoinAnother) { navTo(StatusNotInTeam); }
onClick(OK) { navTo(StatusNotInTeam); }
```

### UI ? service ???

UI ????? Join/Pair Member ???????

* `uiCreateTeam()`
* `startPairingLeader()/startPairingMember()`
* `stopPairing()`
* `uiLeaveTeam()` / `uiKickMember()` / `uiTransferLeader()`

---



## 4.2 ??/??????Pairing & KeyDist?

### 4.2.1 ESP?NOW Pairing ??

- Leader??? Pairing ?? -> ?? Beacon -> ?? Join ??? TEAM_KEY_DIST
- Member??? Beacon -> Join Sent -> Waiting Key -> Joined
- ??????? / ?? -> ?? StatusNotInTeam

### 4.2.2 KeyDist ??

- ?? TEAM_KEY_DIST ??? key??? snapshot??? Joined

---

## 4.2.10 Kick Flow（Leader → 全队）

```cpp
void TeamService::uiKickMember(MemberId target) {
  if (!model_.isLeader()) return;

  // 1) event: MemberKicked(target)
  appendEventAndApply(makeMemberKickedEvent(target));

  // 2) epoch rotate
  rotateEpoch();

  // 3) broadcast kick + epoch rotate marker（可合并成一个 control 包）
  transport_.send(encodeKick(model_.s.team_id, model_.s.epoch, target));

  // 4) distribute new key to remaining members
  distributeEpochKeyToMembers();
}
```

### 被踢成员的处理（Member 侧）

```cpp
void TeamService::onKick(const Kick& k) {
  if (!model_.s.in_team) return;
  if (k.target != selfNodeId()) {
    // 其他人被踢：apply事件、等待key更新
    appendEventAndApply(makeMemberKickedEvent(k.target));
    model_.s.security = TeamSecurityState::WARN;
    return;
  }

  // 自己被踢：立即失效
  model_.s.in_team = false;
  model_.s.security = TeamSecurityState::FAIL;
  crypto_.wipeTeamKeys(model_.s.team_id);
  store_.clearTeam(); // 或标记 revoked
  ui_->navToKickedOut();
}
```

---

## 4.2.11 Presence & Health（状态页数据来源）

```cpp
void TeamService::broadcastPresenceIfDue() {
  if (!model_.s.in_team) return;
  if (clock_.now() - last_presence_ts < policy.presence_period_s) return;

  auto pkt = encodePresence(model_.s.team_id, model_.s.epoch,
                           selfNodeId(),
                           /*event_seq*/ model_.s.last_event_seq,
                           /*battery*/ readBattery(),
                           /*fix*/ gpsFix());
  transport_.send(pkt);
  last_presence_ts = clock_.now();
}

void TeamService::onPresence(const Presence& p) {
  if (!acceptTeam(p.team_id)) return;

  // epoch mismatch: warn + possible sync
  if (p.epoch != model_.s.epoch) {
    model_.s.security = TeamSecurityState::WARN;
    maybeSync(p);
  }

  updateMemberLastSeen(p.sender, p.ts);
  if (p.event_seq > model_.s.last_event_seq) {
    requestSyncFrom(p.sender, model_.s.last_event_seq + 1);
  }
}
```

---

## 4.2.12 Sync（补齐关键事件）

```cpp
void TeamService::requestSyncFrom(MemberId peer, uint32_t from_seq) {
  auto req = encodeSyncReq(model_.s.team_id, model_.s.epoch, from_seq);
  transport_.sendTo(peer, req);
}

void TeamService::onSyncReq(const SyncReq& r, MemberId from) {
  // 从 store 拉最近N条 event 回包
  auto events = store_.readEventsFrom(r.from_seq, policy.sync_max_events);
  transport_.sendTo(from, encodeSyncRsp(model_.s.team_id, model_.s.epoch, events));
}

void TeamService::onSyncRsp(const SyncRsp& rsp) {
  for (auto& e : rsp.events) {
    if (e.event_seq <= model_.s.last_event_seq) continue;
    model_.apply(e);
    store_.appendEvent(e);
  }
  store_.saveSnapshot(model_.s);
  model_.s.security = TeamSecurityState::OK;
}
```

---

# 5) UI 与协议“对齐点清单”（你落地时最有用）

## 5.1 ??????? usecase

* StatusNotInTeam?Create ? `uiCreateTeam()` + `startPairingLeader()`?Join ? `startPairingMember()`
* JoinPending?Pairing??Cancel ? `stopPairing()`?Retry ? `startPairing...()`
* StatusInTeam / TeamHome?Pair Member ? `startPairingLeader()`
* Members/Detail/KickConfirm?Kick/Transfer ? `uiKickMember()/uiTransferLeader()`
* KickedOut?Join Another ? ? StatusNotInTeam

## 5.2 页面字段来自哪里

* Members/Online：`TeamService.snapshot()`
* Security OK/WARN/FAIL：来自

  * 解密成功与否（crypto）
  * epoch 是否一致（presence/控制包）
  * sync 是否完成
* Last update：来自

  * 最近一次 presence/控制事件处理时间

---

# 6) 你可以直接照这个开工的“第一批文件”

如果你想最快把骨架跑起来，我建议按这个顺序建文件：

1. `domain/team_types.h` + `domain/team_events.h`
2. `domain/team_model.cpp`（apply 事件）
3. `ports/i_team_transport.h`（send / sendTo / onPacket）
4. `usecase/team_service.cpp`（先只做 create + invite 广播 + presence）
5. `ui/screens/team/team_state.h` + `team_pages.cpp`（StatusNotInTeam / StatusInTeam 两页先跑起来）
6. 再加 join/kick/sync
