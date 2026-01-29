# A. 全部页面一览（按状态完整覆盖）

> 设备：**2.33-inch 横屏 222×480**
> 约束：**固定 TopBar（Back / Title / Battery）**

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
│   • No team awareness                        │
│                                              │
│   Create or join a team                      │
│                                              │
├──────────────────────────────────────────────┤
│  [ Create Team ]      [ Join Team ]           │
└──────────────────────────────────────────────┘
```

---

## A2. Team 状态入口页（已加入）

**Title：`Team Status`**

```
┌──────────────────────────────────────────────┐
│ < Back            Team Status             🔋 │
├──────────────────────────────────────────────┤
│  Team: ALPHA-7                                │
│  Role:        Member                          │
│  Members:     5                               │
│  Online:      3                               │
│  Security:    OK   (Round 5)                  │
│                                              │
├──────────────────────────────────────────────┤
│  Team Health                                  │
│  ──────────────────────────────────────────  │
│  ● Leader online                              │
│  ● Last update 18s ago                        │
│  ○ 1 member stale                             │
│                                              │
├──────────────────────────────────────────────┤
│  [ View Team ]     [ Invite ]     [ Leave ]  │
└──────────────────────────────────────────────┘
```

> 这是 **判断“队伍是否还可信”** 的页面
> 不是管理页

---

## A3. Team Home（成员与结构，Leader/Member 通用）

**Title：`Team · Leader` 或 `Team · Member`**

```
┌──────────────────────────────────────────────┐
│ < Back          Team · Leader              🔋 │
├──────────────────────────────────────────────┤
│  Team: ALPHA-7                                │
│  Members: 3      Online: 2                   │
│  Security Round: 5                            │
│                                              │
├──────────────────────────────────────────────┤
│  Members                                     │
│  ──────────────────────────────────────────  │
│  ● You (Leader)        Online                │
│  ● Tom                 Online                │
│  ○ Jerry               Last seen 2m ago      │
│                                              │
├──────────────────────────────────────────────┤
│  [ Invite ]        [ Manage ]     [ Leave ]  │
└──────────────────────────────────────────────┘
```

---

## A4. Invite 页面（Leader）

**Title：`Invite`**

```
┌──────────────────────────────────────────────┐
│ < Back               Invite               🔋 │
├──────────────────────────────────────────────┤
│   Invite Code                                │
│                                              │
│        J4K – 9Q2                              │
│                                              │
│   Expires in: 08:45                           │
│                                              │
│   Nearby devices can join                    │
│                                              │
├──────────────────────────────────────────────┤
│  [ Refresh Code ]      [ Stop Invite ]       │
└──────────────────────────────────────────────┘
```

---

## A5. Join Team（扫描 / 输入邀请码）

**Title：`Join Team`**

```
┌──────────────────────────────────────────────┐
│ < Back               Join Team            🔋 │
├──────────────────────────────────────────────┤
│  Nearby Teams                                │
│  ──────────────────────────────────────────  │
│  ALPHA-7          Signal: ▮▮▮▯               │
│  BETA-3           Signal: ▮▮▯▯               │
│                                              │
│  Or enter invite code                        │
│                                              │
├──────────────────────────────────────────────┤
│  [ Enter Code ]        [ Refresh ]            │
└──────────────────────────────────────────────┘
```

---

## A6. Join Pending（等待批准）

**Title：`Join Request`**

```
┌──────────────────────────────────────────────┐
│ < Back           Join Request             🔋 │
├──────────────────────────────────────────────┤
│                                              │
│   Request sent to ALPHA-7                    │
│                                              │
│   Waiting for approval...                    │
│                                              │
│   This may take a moment                     │
│                                              │
├──────────────────────────────────────────────┤
│  [ Cancel ]            [ Retry ]              │
└──────────────────────────────────────────────┘
```

---

## A7. Members 管理页（Leader）

**Title：`Members`**

```
┌──────────────────────────────────────────────┐
│ < Back             Members               🔋 │
├──────────────────────────────────────────────┤
│  ● You (Leader)                              │
│                                              │
│  ● Tom                                      │
│     > Select                                 │
│                                              │
│  ○ Jerry                                    │
│     > Select                                 │
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
│  Status:   Last seen 2m ago                  │
│  Role:     Member                            │
│                                              │
│  Device:   Pager                             │
│                                              │
│  Capability:                                │
│   • Position                                 │
│   • Waypoint                                 │
│                                              │
├──────────────────────────────────────────────┤
│  [ Kick ]        [ Transfer Leader ]         │
└──────────────────────────────────────────────┘
```

---

## A9. Kick 确认页（安全关键）

**Title：`Kick Member`**

```
┌──────────────────────────────────────────────┐
│ < Back             Kick Member            🔋 │
├──────────────────────────────────────────────┤
│  Remove Jerry from team?                     │
│                                              │
│  This will update the security round.        │
│  Jerry will no longer receive                │
│  team messages or waypoints.                 │
│                                              │
├──────────────────────────────────────────────┤
│  [ Cancel ]              [ Confirm Kick ]    │
└──────────────────────────────────────────────┘
```

---

## A10. 被踢 / 失效状态页（Member）

**Title：`Team`**

```
┌──────────────────────────────────────────────┐
│ < Back               Team                🔋 │
├──────────────────────────────────────────────┤
│  You are no longer in this team               │
│                                              │
│  Access to team data revoked                  │
│                                              │
├──────────────────────────────────────────────┤
│  [ Join Another Team ]     [ OK ]             │
└──────────────────────────────────────────────┘
```

---

# B. 页面流转说明（UI 状态机）

## B1. 顶层流转（简化）

```
[ Team Menu ]
     |
     v
[ Team Status ]
     |
     +--> (not in team) --> [ Create Team ] -> [ Team Status (joined) ]
     |
     +--> (joined) -------> [ View Team ] -> [ Team Home ]
```

---

## B2. 加入流程（Member）

```
[ Team Status (not in team) ]
        |
        v
    [ Join Team ]
        |
        v
 [ Join Pending ]
        |
        +-- reject/timeout --> [ Join Team ]
        |
        +-- accept ----------> [ Team Status (joined) ]
```

---

## B3. 邀请流程（Leader）

```
[ Team Home ]
     |
     v
  [ Invite ]
     |
     +-- member joins --> [ Join Request popup ]
                              |
                              +-- accept --> epoch rotate
                              |               |
                              |               v
                              |        [ Team Status ]
                              |
                              +-- reject
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

## C1. 协议消息类型（最小集）

| 类型                   | 用途      |
| -------------------- | ------- |
| `TEAM_INVITE`        | 广播邀请码   |
| `TEAM_JOIN_REQ`      | 申请加入    |
| `TEAM_JOIN_DECISION` | 同意 / 拒绝 |
| `TEAM_KICK`          | 踢人事件    |
| `TEAM_EPOCH_ROTATE`  | 宣告轮次更新  |
| `TEAM_KEY_DIST`      | 新密钥分发   |
| `TEAM_PRESENCE`      | 在线状态    |
| `TEAM_POS`           | 位置      |
| `TEAM_SYNC_REQ`      | 补齐请求    |
| `TEAM_SYNC_RSP`      | 补齐响应    |

---

## C2. TeamEnvelope（所有 Team 包统一外壳）

```
TeamEnvelope {
  team_id
  epoch
  type
  sender_id
  seq
  timestamp
  auth
  payload
}
```

---

# D. 协议流转（和 UI 的对应关系）

## D1. Create Team

* 本地生成 `team_id`
* `epoch = 1`
* 自己 = leader
* 生成 `team_key(1)`

---

## D2. Invite / Join

**Invite**

* Leader 周期发 `TEAM_INVITE`

**Join**

1. Member → `TEAM_JOIN_REQ`
2. Leader → `TEAM_JOIN_DECISION(ACCEPT)`
3. Leader → `epoch++`
4. Leader → `TEAM_KEY_DIST(epoch)`
5. 双方进入新 epoch

---

## D3. Kick

1. Leader → `TEAM_KICK(target)`
2. Leader → `epoch++`
3. Leader → `TEAM_KEY_DIST(epoch)`
4. 被踢成员解密失败 → UI 切到 A10

---

## D4. Presence & Health

* 所有成员周期发 `TEAM_PRESENCE`
* Status 页字段来自：

  * last_seen_ts
  * leader presence
  * epoch 一致性

---

## D5. Sync（补齐）

1. 发现 `event_seq` 落后
2. → `TEAM_SYNC_REQ(from_seq)`
3. → `TEAM_SYNC_RSP(events...)`
4. 重放事件，UI 刷新

---

下面我把前面那套 **Pager Team Core v0.1（入队/踢人/角色 + 状态页流转 + 协议流转）**，直接“落”到你工程里：**模块清单（按你 domain/usecase/ports/infra/ui 的风格）+ 状态机伪代码**。我会尽量贴近你 Trail Mate 的架构习惯（纯 domain、usecase 编排、ports 抽象、infra 适配 Meshtastic、UI 独立状态）。

---

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
    team_join_flow.h/.cpp
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
      team_nav.h/.cpp            // 简单导航（Status/Home/Invite/Join...）
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

## 3.1 TeamService（唯一入口）

> 你可以像 ChatService 一样做一个 TeamService：UI 和 MeshAdapter 都只跟它交互。

```cpp
class TeamService {
public:
  TeamService(ITeamStore&, ITeamTransport&, ITeamCrypto&, IClock&, IRng&, IUiNotifier*);

  // UI actions
  void uiCreateTeam();
  void uiOpenInvite();
  void uiStopInvite();
  void uiJoinByCode(std::string code);
  void uiJoinNearby(TeamId team, std::string hint);
  void uiCancelJoin();
  void uiRetryJoin();
  void uiLeaveTeam();
  void uiKickMember(MemberId target);
  void uiTransferLeader(MemberId target);

  // Radio input
  void onTeamPacket(const uint8_t* data, size_t len, uint32_t from_node);

  // Tick (timers): presence broadcast, invite broadcast, stale detection, retry
  void tick1s();

  // UI data
  TeamState snapshot() const;
  TeamHealth computeHealth() const;

private:
  TeamModel model_;
  // ports
  ITeamStore& store_;
  ITeamTransport& transport_;
  ITeamCrypto& crypto_;
  IClock& clock_;
  IRng& rng_;
  IUiNotifier* ui_;

  // runtime: join flow state, invite state, pending requests etc.
  JoinRuntime join_;
  InviteRuntime invite_;
  SyncRuntime sync_;

  // helpers
  void persistSnapshotSoon();
  void appendEventAndApply(const TeamEvent& e);
  void broadcastPresenceIfDue();
  void broadcastInviteIfDue();
};
```

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
  Invite,
  JoinSelect,
  JoinPending,
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
onClick(CreateTeam) { teamService.uiCreateTeam(); ui.busy=true; }
onClick(JoinTeam)   { navTo(JoinSelect); }

// StatusInTeam
onClick(ViewTeam)   { navTo(TeamHome); }
onClick(Invite)     { if (isLeader) navTo(Invite); else toast("Only leader"); }
onClick(Leave)      { teamService.uiLeaveTeam(); navTo(StatusNotInTeam); }

// TeamHome
onClick(Invite)     { ... }
onClick(Manage)     { if (isLeader) navTo(Members); }
onClick(Leave)      { ... }

// JoinSelect
onSelect(NearbyTeam) { teamService.uiJoinNearby(team_id, hint); navTo(JoinPending); }
onClick(EnterCode)   { navTo(JoinSelectInputCode); } // 可用同页输入
onClick(JoinByCode)  { teamService.uiJoinByCode(code); navTo(JoinPending); }

// JoinPending
onClick(Cancel) { teamService.uiCancelJoin(); navTo(JoinSelect); }
onClick(Retry)  { teamService.uiRetryJoin(); }

// Members
onSelect(Member) { ui.selected_member=...; navTo(MemberDetail); }

// MemberDetail
onClick(Kick) { navTo(KickConfirm); }
onClick(TransferLeader) { teamService.uiTransferLeader(ui.selected_member); navTo(TeamHome); }

// KickConfirm
onClick(ConfirmKick) { teamService.uiKickMember(ui.selected_member); navTo(StatusInTeam); }

// KickedOut
onClick(JoinAnother) { navTo(JoinSelect); }
onClick(OK) { navTo(StatusNotInTeam); }
```

### UI 与 service 的绑定

UI 不直接判断“安全轮次如何更新”，它只触发：

* `uiKickMember(target)`
* `uiJoinByCode(code)`
  由 service 做协议与状态更新，UI只订阅 `snapshot()` 变化。

---

## 4.2 协议/业务状态机（Join/Kick/Rotate/Sync）

### 4.2.1 Join Flow Runtime

```cpp
enum class JoinPhase {
  Idle,
  Discovering,      // 可选：扫描附近 invite
  Requested,        // 已发 JOIN_REQ
  WaitingDecision,  // 等 ACCEPT/REJECT
  WaitingKey,       // 等 KEY_DIST
  Joined,           // 成功
  Failed
};

struct JoinRuntime {
  JoinPhase phase = JoinPhase::Idle;
  TeamId target_team;
  uint32_t started_ts = 0;
  uint8_t retries = 0;
  std::string invite_code;
};
```

### 4.2.2 Invite Flow Runtime（Leader）

```cpp
struct InviteRuntime {
  bool active = false;
  std::string invite_code;
  uint32_t expire_ts = 0;
  uint32_t last_broadcast_ts = 0;
};
```

### 4.2.3 核心伪代码：UI Create Team

```cpp
void TeamService::uiCreateTeam() {
  // 1) domain init
  model_.s.in_team = true;
  model_.s.team_id = rng_.randomTeamId();
  model_.s.epoch = 1;
  model_.s.self_role = TeamRole::Leader;
  model_.s.leader_id = selfNodeId();

  // 2) crypto init
  crypto_.setTeamKey(model_.s.team_id, model_.s.epoch, crypto_.randomKey());

  // 3) persist snapshot (store)
  store_.saveSnapshot(model_.s);
  // 4) optional: append TeamCreated event (event_seq=1)
  appendEventAndApply(makeTeamCreatedEvent());
}
```

---

## 4.2.4 Leader：Invite 广播

```cpp
void TeamService::uiOpenInvite() {
  if (!model_.isLeader()) return;
  invite_.active = true;
  invite_.invite_code = makeShortCode(rng_);
  invite_.expire_ts = clock_.now() + policy.invite_ttl_s;
  invite_.last_broadcast_ts = 0;
}

void TeamService::broadcastInviteIfDue() {
  if (!invite_.active) return;
  if (clock_.now() > invite_.expire_ts) { invite_.active=false; return; }
  if (clock_.now() - invite_.last_broadcast_ts < policy.invite_broadcast_period_s) return;

  auto pkt = encodeInvite(model_.s.team_id, model_.s.epoch, invite_.invite_code, invite_.expire_ts);
  transport_.send(pkt);
  invite_.last_broadcast_ts = clock_.now();
}
```

---

## 4.2.5 Member：Join 请求

```cpp
void TeamService::uiJoinByCode(std::string code) {
  join_.phase = JoinPhase::Requested;
  join_.invite_code = code;
  join_.started_ts = clock_.now();
  join_.retries = 0;

  // 目标 team_id 可能未知：两种做法
  // A) code本身编码team短码 -> 可直接得到 team_id_hint
  // B) 先等待附近 invite 匹配 code -> 得到 team_id
  // v0.1 推荐：code + nearby invite 匹配后再发请求

  sendJoinReq();
}

void TeamService::sendJoinReq() {
  auto req = encodeJoinReq(/*team_id*/ join_.target_team,
                          /*code*/ join_.invite_code,
                          /*self*/ selfNodeId(),
                          /*nonce*/ rng_.u32());
  transport_.send(req);
  join_.phase = JoinPhase::WaitingDecision;
}
```

---

## 4.2.6 Leader：处理 JOIN_REQ（弹窗 + Accept/Reject）

```cpp
void TeamService::onJoinReq(const JoinReq& req, uint32_t from) {
  if (!model_.isLeader()) return;
  if (!invite_.active) return;
  if (clock_.now() > invite_.expire_ts) return;
  if (req.code != invite_.invite_code) return; // v0.1：简单匹配

  // 让 UI 弹窗：Accept / Reject
  ui_->promptJoinRequest(from, /*name*/ req.name);
  // Accept 的动作走 uiAcceptJoin(from)
}

void TeamService::uiAcceptJoin(MemberId newcomer) {
  // 1) append event: MemberAccepted (event_seq++)
  appendEventAndApply(makeMemberAcceptedEvent(newcomer));

  // 2) epoch rotate (成员集合变化)
  rotateEpoch(); // epoch++

  // 3) send decision (带 new_epoch)
  transport_.send(encodeJoinDecisionAccept(model_.s.team_id, model_.s.epoch, newcomer));

  // 4) distribute keys to all current members
  distributeEpochKeyToMembers();
}
```

---

## 4.2.7 Epoch Rotate & Key Dist（v0.1 关键）

```cpp
void TeamService::rotateEpoch() {
  // append event: EpochRotated(new_epoch)
  uint32_t new_epoch = model_.s.epoch + 1;
  appendEventAndApply(makeEpochRotatedEvent(new_epoch));

  // generate key for new epoch
  auto new_key = crypto_.randomKey();
  crypto_.setTeamKey(model_.s.team_id, new_epoch, new_key);
}

void TeamService::distributeEpochKeyToMembers() {
  for (auto& m : model_.s.members) {
    if (m.id == kicked_target) continue;
    // v0.1: 用点对点封装（可以用 Meshtastic channel PSK 或你的设备预共享方式）
    auto pkt = encodeKeyDist(model_.s.team_id, model_.s.epoch, m.id, crypto_.getTeamKey(...));
    transport_.sendTo(m.id, pkt); // 如果 transport 支持定向，否则广播 + 目标字段
  }
}
```

---

## 4.2.8 Member：收到 ACCEPT + KEY_DIST

```cpp
void TeamService::onJoinDecision(const JoinDecision& d) {
  if (join_.phase != JoinPhase::WaitingDecision) return;
  if (!d.accept) { join_.phase = JoinPhase::Failed; ui_->toast("Rejected"); return; }

  // 记录目标 epoch，等待 key
  join_.phase = JoinPhase::WaitingKey;
  join_.expected_epoch = d.new_epoch;
}

void TeamService::onKeyDist(const KeyDist& kd) {
  if (join_.phase != JoinPhase::WaitingKey) return;
  if (kd.epoch != join_.expected_epoch) return;

  crypto_.setTeamKey(kd.team_id, kd.epoch, kd.key);
  // 成功加入：更新本地team snapshot（可以从 decision 携带的 leader_id/成员摘要恢复）
  model_.s.in_team = true;
  model_.s.team_id = kd.team_id;
  model_.s.epoch = kd.epoch;
  model_.s.self_role = TeamRole::Member;
  model_.s.security = TeamSecurityState::OK;

  store_.saveSnapshot(model_.s);
  join_.phase = JoinPhase::Joined;
}
```

---

## 4.2.9 Kick Flow（Leader → 全队）

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

## 4.2.10 Presence & Health（状态页数据来源）

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

## 4.2.11 Sync（补齐关键事件）

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

## 5.1 每个页面触发的 usecase

* StatusNotInTeam：Create → `uiCreateTeam()`；Join → `uiJoin...()`
* JoinSelect：选择附近队伍 → `uiJoinNearby()`；输入 code → `uiJoinByCode()`
* JoinPending：Retry/Cancel → `uiRetryJoin()/uiCancelJoin()`
* Invite：Refresh/Stop → `uiOpenInvite()/uiStopInvite()`
* Members/Detail/KickConfirm：Kick/Transfer → `uiKickMember()/uiTransferLeader()`
* KickedOut：Join Another → 回 JoinSelect

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