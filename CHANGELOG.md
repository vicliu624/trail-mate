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
