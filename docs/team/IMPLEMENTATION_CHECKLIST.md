# Team 实现清单（按你的架构分层）

目标：参考现有 chat 的分层方式，在 Team 功能上采用同样的分层结构，并显式对齐「解释即架构」的开发思想。以下清单按层拆解，便于逐项落地与验收。

## 1) Domain（纯模型层）

- team/domain/team_types.h
  - 基础类型：TeamId, TeamKey, TeamRole, TeamState, TeamMember, TeamParams
  - 事件类型枚举：TeamEventType
  - 事件载体：TeamEvent（包含 event_id/team_id/ts/sender_id/key_id/payload）
- team/domain/team_model.h/.cpp
  - 状态机：Idle/Create/Active/Rotate/Disband
  - 成员可见性/last_seen/health
  - 位置降采样策略
  - 复盘事件生成（纯内存结构，不涉及存储）

## 2) Ports（接口层）

- team/ports/i_team_store.h
  - 读写 PersistentTeamState（单一对象）
  - load/save/erase
- team/ports/i_team_log_store.h
  - 追加写 TeamEvent（LogRecord）
  - appendEvent/appendSnapshot（snapshot 可选）
- team/ports/i_team_mesh_adapter.h
  - sendAppData(portnum, payload, len, dest, want_ack)
  - pollIncomingData 返回 MeshIncomingData
- team/ports/i_team_crypto.h
  - kdf, aeadEncrypt, aeadDecrypt

## 3) Usecase（用例层）

- team/usecase/team_service.h/.cpp
  - createTeam/joinTeam/acceptJoin/confirmJoin
  - rotateKey/disband/leave
  - processIncoming：消费 TEAM_*_APP 数据包
  - 严格遵守持久化时机（成功跃迁后写入，解散开始即删除）
  - 事件落盘（LogStore）与 EventBus 通知

## 4) Infra（基础设施层）

- 存储
  - team/infra/store/team_flash_store.*：NVS/Preferences 单 blob（含 magic/version/length/crc）
  - team/infra/store/team_log_store.*：SD append-only LogRecord（AEAD + CRC）
- Meshtastic 适配
  - team/infra/meshtastic/team_mt_adapter.* 或复用 MtAdapter 新增 pollIncomingData/sendAppData
  - 解析 meshtastic_Data 的 portnum，将非 TEXT 的 payload 入队
  - 定义 TEAM_*_APP 自定义 portnum（基于 PortNum_PRIVATE_APP + n）
- 密码学
  - team/infra/crypto/team_crypto_*：KDF + AEAD（与 TeamEncrypted 结构匹配）

## 5) AppContext 与任务流

- app/app_context.*
  - 新增 TeamModel, TeamService, TeamStore, TeamLogStore
  - AppContext::update 调用 team_service_->processIncoming
- sys/event_bus.h
  - 新增 Team 相关事件类型（状态变化、成员变化、指令、诊断）

## 6) 协议与编码

- TeamEncrypted 统一 Envelope（version/team_id/key_id/nonce/ciphertext/aad_flags）
- TEAM_MGMT_APP
  - TEAM_ADVERTISE, JOIN_REQUEST, JOIN_ACCEPT, JOIN_CONFIRM, TEAM_STATUS
- TEAM_POSITION_APP / TEAM_WAYPOINT_APP
  - 明文 payload 为 meshtastic Position/Waypoint（再用 TeamEncrypted AEAD）

## 7) 持久化与日志

- PersistentTeamState 单对象
  - identity + secrets + channel + params + log_*
- 启动恢复
  - magic/crc 失败 -> erase -> Idle
- LogRecord
  - AEAD(LogKey) + trailer_crc32
  - 关键事件即时 flush

## 8) MVP 优先级（建议）

1. TeamService + TeamModel（状态机）
2. Meshtastic adapter 支持 pollIncomingData/sendAppData
3. TEAM_ADVERTISE / JOIN_REQUEST / JOIN_ACCEPT / TEAM_POSITION_APP
4. NVS 持久化单对象
5. SD LogRecord 追加写（无 snapshot）
6. JOIN_CONFIRM / TEAM_STATUS / ROTATE
