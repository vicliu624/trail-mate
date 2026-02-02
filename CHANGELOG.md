# Changelog / 更新日志

All notable changes to this project will be documented in this file.  
本文件记录项目的重要变更。

This project follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).  
本项目遵循 Keep a Changelog，并使用语义化版本号。

Because the project is still pre-1.0, breaking changes may occur between minor
versions.  
由于目前仍处于 1.0 之前，次版本之间可能包含破坏性变更。

## [Unreleased] / 未发布
### Planned / 计划中
- Settings screens (radio, channels, device) / 设置页面（无线、频道、设备）
- Flash-backed chat storage and message persistence / 基于 Flash 的聊天存储与消息持久化
- Meshtastic encryption (AES/PSK) and broader protocol compatibility / Meshtastic 加密（AES/PSK）与更完整协议兼容
- Real LoRa field tests, performance, and power tuning / 实机 LoRa 测试、性能与功耗调优

## [0.1.4-alpha] - 2026-02-02
### Added / 新增
- T-Deck board support and new build env / T-Deck 板级支持与新构建环境
- GPS tracker/track recorder with Tracker screen and assets / GPS 轨迹记录与 Tracker 页面及资源
- Team NFC support, team chat protocol, and team UI store / Team NFC 支持、团队聊天协议与 Team UI 存储
- Toast widget and LVGL lifetime notes / Toast 组件与 LVGL 生命周期说明

### Changed / 变更
- UI lifecycle refactor with compose flow / UI 生命周期重构与 compose 流程
- GPS page layout/styles and map overlay refactor / GPS 页面布局/样式与地图覆盖层重构
- UI controller and app screen wiring adjustments / UI 控制器与页面接线调整

### Fixed / 修复
- Meshtastic node id handling error / Meshtastic 节点 ID 处理错误
- UI glitches across chat/contacts/gps/team / Chat/Contacts/GPS/Team 页面 UI 问题修复

## [0.1.3-alpha] - 2026-01-23
### Added / 新增
- Team management stack (domain/protocol/service) and Team screen / Team 管理模块（领域/协议/服务）与 Team 页面
- Team docs/assets / Team 文档与资源
- Send-status UI and message preview truncation / 发送状态 UI 与消息预览截断

### Changed / 变更
- Mesh persistence and PKI handling (node store/backoff) / Mesh 持久化与 PKI 处理（节点存储/退避）
- Chat storage improvements across RAM/Flash/log stores / 聊天存储改进（RAM/Flash/日志）
- Diagnostics and timestamp utilities / 诊断信息与时间戳工具改进

### Fixed / 修复
- Messaging reliability across reboots / 重启后消息可靠性
- UI timezone offset application / UI 时区偏移应用

## [0.1.2-alpha] - 2026-01-21
### Added / 新增
- Meshtastic routing ACK decoding with reason mapping, ack timeouts, and richer RX/TX diagnostics / Meshtastic 路由 ACK 解码，增加原因映射、ACK 超时和更详细的收发日志
- PKI peer key persistence and per-node PKI backoff after NO_CHANNEL/UNKNOWN_PUBKEY / PKI 公钥持久化，对 NO_CHANNEL/UNKNOWN_PUBKEY 加入单节点 PKI 退避
- Time helpers for chat timestamps / 聊天时间戳辅助工具

### Changed / 变更
- Chat app switching now calls exit callbacks and clears active app on menu return / Chat 应用切换支持 exit 回调，返回菜单清空 active app
- Contacts compose flow supports broadcast channels and avoids UI tree mismatch when chat container exists / Contacts 撰写支持广播频道，在 chat container 存在时避免 UI 树错位
- UI timestamp display applies stored timezone offset / UI 显示时间应用已存储时区偏移

### Fixed / 修复
- Chat screen cleanup safety on repeated enter/exit to prevent black-screen stacking / Chat 重复进出时更安全清理，避免黑屏堆叠

## [0.1.1-alpha] - 2026-01-20
### Added / 新增
- Mesh protocol factory with explicit Meshtastic/MeshCore selection / Mesh 协议工厂，支持显式选择 Meshtastic/MeshCore
- Settings actions: reset mesh params, reset node DB, clear message DB / 设置操作：重置 Mesh 参数、重置节点库、清空消息库
- Meshtastic region utilities and docs (region table, frequency helpers, protocol docs) / Meshtastic 区域工具与文档（区域表、频率计算、协议文档）

### Changed / 变更
- Settings: protocol/region changes now restart immediately / 设置：协议/区域修改后立即重启
- Config sync: protocol/region/preset and PSK wired to AppConfig/applyConfig / 配置同步：协议/区域/预设与 PSK 接入 AppConfig/applyConfig
- UI assets and README screenshots refreshed / UI 资源与 README 截图更新

### Fixed / 修复
- CN region frequency calculation alignment for LongFast / CN 区域 LongFast 频点计算对齐

## [0.1.0-alpha] - 2026-01-18
### Added / 新增
- Offline GPS mapping with fixed north-up orientation, zoom levels, and breadcrumb trails / 离线 GPS 地图（固定北向上、缩放级别、面包屑轨迹）
- LoRa text chat compatible with Meshtastic (simplified codec + dedup) / 兼容 Meshtastic 的 LoRa 文本聊天（简化编解码 + 去重）
- Chat architecture layers (domain/usecase/ports/infra), RAM store, and mock adapter / 聊天分层架构（领域/用例/端口/基础设施）、RAM 存储与 Mock 适配器
- Chat UI: channel list + conversation screens, integrated into the main UI / 聊天 UI（频道列表 + 会话页），已集成到主界面
- App context/config, event bus, ring buffer, and task scaffolding / 应用上下文与配置、事件总线、环形缓冲区、任务框架
- English/Chinese README plus supporting design docs / 中英文 README 与设计文档

### Notes / 说明
- This is an initial, pre-release build; several features are intentionally incomplete. / 这是初始预发布版本，部分功能仍未完成。
