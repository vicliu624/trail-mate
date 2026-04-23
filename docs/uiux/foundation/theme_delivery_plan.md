# Theme and Presentation Delivery Plan

## 1. Purpose

本文不是再重复一遍规格，而是把主题 / presentation 系统从“当前分支已经做到哪里”到“还必须补哪些阶段”整理成一份可执行交付计划。

本文的目标有三个：

- 把 `specification target` 和 `runtime reality` 明确分开
- 把剩余工作按阶段收口，避免后续重新发散成零散补丁
- 为每个阶段写清楚 `Definition of Done` 和验证要求

如果本文与以下上位文档冲突，以上位文档为准：

- [theme_system_spec.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/theme_system_spec.md)
- [theme_authoring_guide.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/theme_authoring_guide.md)
- [theme_slot_inventory.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/theme_slot_inventory.md)
- [presentation_contract_inventory.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/presentation_contract_inventory.md)

---

## 2. Distinction Baseline

在推进计划前，先把几个容易再次混掉的对象固定下来：

- `theme runtime completion`
  指 token / component profile / semantic asset binding 真正进入运行时，而不是只写进规格。
- `presentation runtime completion`
  指外部 `presentation-pack` 不只是“能被安装和枚举”，而是真正能以公开 contract 驱动 archetype 的布局呈现。
- `archetype migration`
  指某一类页面族已经从页面私有布局迁到 archetype 语义入口，而不是只有文档里声明了 contract。
- `official sample pack`
  指官方自己先通过外部包跑通 end-to-end 链路，作为第三方作者的真实样板。
- `fallback baseline`
  指在任何包失效、缺失或损坏时，固件仍保有最低可用界面，不因第三方包损坏而不可用。

因此，后续所有阶段都必须同时回答三个问题：

1. 这一步是在补 `theme`、`presentation`，还是在补 archetype migration？
2. 这一步新增的是 `public contract`，还是只是 `builtin implementation`？
3. 这一步失败时，fallback 是否仍然成立？

---

## 3. Current Baseline

截至当前分支，已经稳定成立的基线如下：

### 3.1 Completed Foundations

- `theme-pack` 与 `presentation-pack` 已成为正式扩展对象，catalog / install / uninstall / settings / startup 都能识别。
- 官方外部样板包已经存在：
  - [official-themes](/C:/Users/VicLi/Documents/Projects/trail-mate/packs/ui-bundles/official-themes)
  - [official-presentations](/C:/Users/VicLi/Documents/Projects/trail-mate/packs/ui-bundles/official-presentations)
- 运行时公共边界已经分清：
  - `official-*` 与第三方自定义 ID 是 public selection surface
  - `builtin-ascii` 是公开允许依赖的 builtin baseline
  - `builtin-legacy-warm`、`builtin-default`、`builtin-directory-stacked` 只属于 fallback / compatibility surface

### 3.2 Completed Theme Runtime Surface

当前已经真正进入运行时的 theme 表面：

- `tokens.ini`
  当前已消费 `color.*`
- `components.ini`
  当前已消费首批共享组件 profile
- `assets.ini`

Current live semantic color extraction batch also includes:

- `color.map.route`
- `color.map.track`
- `color.map.marker.border`
- `color.map.signal_label.bg`
- `color.map.node_marker`
- `color.map.self_marker`
- `color.map.link`
- `color.map.readout.id`
- `color.map.readout.lon`
- `color.map.readout.lat`
- `color.map.readout.distance`
- `color.map.readout.protocol`
- `color.map.readout.rssi`
- `color.map.readout.snr`
- `color.map.readout.seen`
- `color.map.readout.zoom`
- `color.team.member.0`
- `color.team.member.1`
- `color.team.member.2`
- `color.team.member.3`
- `color.gnss.system.gps`
- `color.gnss.system.gln`
- `color.gnss.system.gal`
- `color.gnss.system.bd`
- `color.gnss.snr.good`
- `color.gnss.snr.fair`
- `color.gnss.snr.weak`
- `color.gnss.snr.not_used`
- `color.gnss.snr.in_view`
- `color.sstv.meter.mid`
  当前已消费一部分语义资源槽位

首批已经 runtime-backed 的 component profile 批次包括：

- `component.top_bar.container`
- `component.top_bar.back_button`
- `component.top_bar.title_label`
- `component.top_bar.right_label`
- `component.menu.app_button`
- `component.menu.app_label`
- `component.menu.bottom_chip.node`
- `component.menu.bottom_chip.ram`
- `component.menu.bottom_chip.psram`
- `component.menu_dashboard.app_grid`
- `component.menu_dashboard.bottom_chips`
- `component.directory_browser.selector_panel`
- `component.directory_browser.content_panel`
- `component.directory_browser.selector_button`
- `component.directory_browser.list_item`
- `component.info_card.base`
- `component.info_card.header`
- `component.busy_overlay.panel`
- `component.busy_overlay.progress_bar`
- `component.notification.banner`
- `component.boot_splash.root`
- `component.boot_splash.log`
- `component.watch_face.root`
- `component.watch_face.primary_region`
- `component.watch_face.node_id`
- `component.watch_face.battery`
- `component.watch_face.clock.hour`
- `component.watch_face.clock.minute`
- `component.watch_face.clock.separator`
- `component.watch_face.date`
- `component.chat_conversation.root`
- `component.chat_conversation.thread_region`
- `component.chat_conversation.action_bar`
- `component.chat.bubble.self`
- `component.chat.bubble.peer`
- `component.chat.bubble.text`
- `component.chat.bubble.time`
- `component.chat.bubble.status`
- `component.chat_compose.root`
- `component.chat_compose.content_region`
- `component.chat_compose.editor`
- `component.chat_compose.action_bar`
- `component.action_button.primary`
- `component.action_button.secondary`
- `component.map_focus.root`
- `component.map_focus.overlay.primary`
- `component.map_focus.overlay.secondary`
- `component.instrument_panel.root`
- `component.instrument_panel.body`
- `component.instrument_panel.canvas_region`
- `component.instrument_panel.legend_region`
- `component.service_panel.root`
- `component.service_panel.body`
- `component.service_panel.primary_panel`
- `component.service_panel.footer_actions`
- `component.modal.scrim`
- `component.modal.window`
- `component.team.member_chip`
- `component.gnss.sky_panel`
- `component.gnss.status_panel`
- `component.gnss.status_header`
- `component.gnss.table_header`
- `component.gnss.table_row`
- `component.gnss.status_toggle`
- `component.gnss.satellite_use_tag`
- `component.sstv.image_panel`
- `component.sstv.progress_bar`
- `component.sstv.meter_box`

This live component batch now covers shared chrome, archetype scaffold surfaces, and the first
page-private extraction batch for tracker / team / GNSS / SSTV / GPS / map.

### 3.3 Completed Presentation Runtime Surface

- archetype contract 已经建立，并同步进入文档与代码 catalog。
- `directory_browser` 已有可运行的 builtin presentation 分支，且已通过真机行为验证。
- 其余 archetype 已进入 facade / registry / official pack 覆盖范围，但成熟度仍不一致。

### 3.4 Still Incomplete

以下几项必须明确视为“还没完成”，不能因为文档存在就误判为已完成：

- `font.* / metric.* / motion.*` 还没有成为完整 external theme runtime surface
- `components.ini` 还没有覆盖全部 historical UI；当前只完成了共享组件批次和 tracker / team / GNSS / SSTV / GPS / map 的首批 page-private 提取批次
- `presentation-pack` 仍偏向 archetype-level variant/runtime facade，还不是完整 declarative schema runtime
- `menu_dashboard / boot_splash / watch_face` 虽然已进入首批 declarative schema batch，但还没有成熟的多变体 presentation 运行时
- `builtin-ascii` 已经剥离共享 boot / status / menu / notification bitmap 批次，也清掉了 page-private marker bitmap fallback，当前只保留最小 text/symbol fallback

---

## 4. Delivery Strategy

后续工作必须按“先收口 runtime surface，再深化 archetype 成熟度，最后压缩 fallback 依赖”的顺序推进。

不允许再走这类错误路径：

- 继续零散给单个页面补 presentation 特例
- 在未补齐 theme runtime surface 前，继续扩写 authoring capability
- 在未建立 declarative presentation runtime 前，把更多变体硬编码进单个 renderer
- 在 external 官方包还未跑稳前，贸然删除 builtin fallback

---

## 5. Phases

### Phase 0. Boundary and Contract Baseline

目标：

- 明确 `theme != presentation`
- 明确 public ID 与 builtin fallback 边界
- 建立 theme / presentation contract catalog

当前状态：

- `completed`

完成定义：

- 规格、inventory、authoring guide、registry、settings、startup 对同一套术语一致

验证：

- 文档同步完成
- public ID / builtin ID 行为一致

### Phase 1. External Pack Runtime Baseline

目标：

- `theme-pack` / `presentation-pack` 能被安装、识别、枚举、激活、回退

当前状态：

- `completed`

完成定义：

- pack repository 能产出 catalog
- settings / startup / extensions 能识别外部包
- 默认官方体验优先走 external 官方包，失败时安全回退 builtin

验证：

- catalog 构建通过
- 固件构建通过

### Phase 2. Archetype Runtime Baseline

目标：

- archetype contract 从文档进入运行时 facade

当前状态：

- `completed for baseline`

说明：

- 这一步的完成含义是“所有 public archetype 已进入统一 contract 与官方样板覆盖面”，不是“全部 archetype 都已经成熟到可自由替换布局”。

完成定义：

- public archetype 进入 code contract catalog
- archetype facade 已被主要页面族接入
- official presentation pack 对全部 public archetype 有显式覆盖

验证：

- 固件构建通过
- 官方 presentation pack 打包通过

### Phase 3. Theme Runtime Surface Completion

目标：

- 把 external theme 从“颜色 + 首批组件 + 部分资源”推进到完整可依赖表面

当前状态：

- `in progress`

本阶段子项：

1. 已完成：`components.ini` 共享组件 runtime，以及 tracker / team / GNSS / SSTV / GPS / map 的首批 page-private 运行时提取
2. 待完成：扩展更多共享组件 profile
3. 待完成：把 `font.*` 落进 runtime
4. 待完成：把 `metric.*` 落进 runtime
5. 待完成：把 `motion.*` 落进 runtime
6. 待完成：明确哪些 page-level style 仍应上收为公共 `component.*`

完成定义：

- authoring guide 中列出的 theme contract 至少大部分已进入真实 runtime
- 第三方作者不再只能依赖颜色与少量图片槽位
- 官方 theme pack 可以不靠内建主题 helper 表达主要共享外观

验证：

- 官方 theme pack 打包通过
- 固件构建通过
- 至少一轮真机验证覆盖 theme 切换 / 重启恢复 / 卸载回退

### Phase 4. Declarative Presentation Runtime

目标：

- 把 `presentation-pack` 从 variant 解析推进到 declarative schema runtime

当前状态：

- `in progress`
- `page_archetype.directory_browser` 已进入第一批 declarative schema pilot
- `page_archetype.chat_conversation` 与 `page_archetype.chat_compose` 已进入第一批 declarative schema batch
- `page_archetype.map_focus` 已进入第一批 declarative schema batch
- `page_archetype.instrument_panel` 与 `page_archetype.service_panel` 已进入第一批 declarative schema batch
- `page_archetype.menu_dashboard`、`page_archetype.boot_splash` 与 `page_archetype.watch_face` 已进入第一批 declarative schema batch
- external `presentation-pack` 已开始执行首条 validator hardening 规则：manifest 中声明的 `layout.*` 文件若解析失败，则整个 presentation 不进入运行时选择面
- external `theme-pack` 已开始执行首条 validator hardening 规则：manifest 中声明的 `tokens / components / assets` 文件若解析失败，则整个 theme 不进入运行时选择面

本阶段子项：

1. 扩展 v1 schema parser 到更多 archetype
2. 收紧 validator，并把 unknown key / invalid value / missing required region 拒绝策略固化
3. 扩展 archetype renderer 与 region/component/binding/action 映射
4. 建立更完整的 schema failure fallback
5. 让官方 presentation pack 逐步形成多 archetype 的 declarative 样板

完成定义：

- 外部包不只是写 `split / stacked / standard`
- 外部包能围绕公开 `page_archetype.* / region.* / component.* / binding.* / action.*` 描述布局
- validator 能拒绝未知 ID、缺失 required region、关键 action 不可达等非法 schema

验证：

- schema parser 单元验证通过
- 固件构建通过
- 真机验证通过

### Phase 5. Archetype Maturity Deepening

目标：

- 让每个 public archetype 的运行时成熟度显式提升，而不是长期停留在 facade 层

当前状态：

- `not started as a unified pass`

推进顺序：

1. `page_archetype.directory_browser`
2. `page_archetype.chat_conversation` / `page_archetype.chat_compose`
3. `page_archetype.map_focus`
4. `page_archetype.instrument_panel` / `page_archetype.service_panel`
5. `page_archetype.menu_dashboard` / `page_archetype.boot_splash` / `page_archetype.watch_face`

完成定义：

- 每个 archetype 满足 [theme_system_spec.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/theme_system_spec.md) 中的 archetype Definition of Done
- 成熟度矩阵持续同步
- 不再把“contract 已定义”误写成“运行时已完成”

验证：

- 对应页面族构建通过
- 对应 archetype 的 external 官方样板可安装、可切换、可回退
- 至少一轮真机回归

### Phase 6. Fallback Compression and Cleanup

目标：

- 让 `builtin-ascii` 成为真正最小可用 fallback
- 继续压缩旧 builtin 官方路径的主路径角色

当前状态：

- `not started`

本阶段子项：

1. 继续压缩 `builtin-ascii` 的 text/symbol fallback 表达，并验证官方外部主题缺失时的最小可用体验
2. 审查运行时中仍把 `builtin-legacy-warm / builtin-default / builtin-directory-stacked` 当主路径的残留逻辑
3. 收缩 compatibility alias，而不是直接破坏旧设置恢复

完成定义：

- 无外部包时，固件依然可用
- external 官方包缺失或损坏时，fallback 明确可用
- fallback 不再承载“官方默认体验主路径”

验证：

- 冷启动无包验证
- 包损坏验证
- 卸载官方包后的恢复验证

### Phase 7. Release-Grade Verification

目标：

- 把这套系统从“开发分支可跑”收口到“可对外开放”

当前状态：

- `not started`

本阶段子项：

1. 建立最小自动验证矩阵
2. 跑通 end-to-end 设备闭环
3. 收敛 pack build 噪音与作者可预期性

完成定义：

- 至少覆盖以下链路：
  - 安装 theme-pack
  - 安装 presentation-pack
  - settings 切换
  - 重启恢复
  - 卸载回退
  - 包缺失 / 非法 schema / 不兼容 manifest 回退

验证：

- 固件构建通过
- pack catalog 构建通过
- 真机闭环通过

---

## 6. Current Milestone

当前里程碑定义为：

> 把 `components.ini` 从规格条目推进为真实 runtime capability，并把它接入首批共享组件与官方外部 theme 样板。

当前状态：

- `completed`

本里程碑已交付：

- component slot registry 已建立
- `components.ini` 已进入 theme runtime
- 首批共享组件已接入 component profile
- 官方 `theme-pack` 已声明并提供 `components.ini`
- spec / authoring guide / inventory 已同步到当前 runtime reality

本里程碑验证要求：

- 固件构建通过
- 官方 pack catalog 重建通过

---

## 7. Immediate Next Milestone

下一优先级最高的里程碑不再是扩展新的 archetype batch，而是：

> 在 public archetype 已经首批全覆盖之后，收紧 validator、固化 schema failure fallback，并开始压缩 fallback 依赖。

推荐试点：

- unknown key / invalid value / missing required region 的拒绝策略
- external pack 损坏或字段缺失时的 fallback 行为
- `builtin-ascii` 与旧 builtin 官方路径的进一步收缩

原因：

- archetype 覆盖面已经接近完整，接下来最容易失控的是 schema 失败路径和兼容回退
- 如果 validator 和 fallback 不先收口，第三方 authoring surface 会继续扩大，但设备端行为边界不够硬
- 这一步会把系统从“能跑的开发链”推进到“更可对外依赖的运行时”

完成后再进入：

- 发布级验证与 release checklist 收口

---

## 7.1 Progress Update

The plan state above should now be read with this concrete runtime update:

- `Phase 4` is no longer `not started`; the first declarative presentation runtime pilot is now active.
- The active pilot archetype is `page_archetype.directory_browser`.
- Current `directory_browser` runtime already parses a first v1 declarative schema batch from external `presentation-pack` layout files.
- `page_archetype.chat_conversation` and `page_archetype.chat_compose` now also parse a first declarative schema batch for action-region ordering and geometry.
- `page_archetype.map_focus` now also parses a first declarative schema batch for overlay-region geometry.
- `page_archetype.instrument_panel` and `page_archetype.service_panel` now also parse a first declarative schema batch for archetype scaffold geometry.
- `page_archetype.menu_dashboard`, `page_archetype.boot_splash`, and `page_archetype.watch_face` now also parse a first declarative schema batch for display-surface geometry.
- The next priority is no longer "extend the last contract-only archetype group", but "tighten validator / fallback discipline on top of the now-near-complete archetype coverage".

Recommended next archetype order after this update:

- validator hardening
- schema failure fallback
- fallback compression

## 8. Rule for Future Turns

后续任何继续开发此系统的工作，都应先对照本文回答：

1. 我现在是在推进哪一个 phase？
2. 这一步会不会偷偷改写 public/builtin 边界？
3. 这一步的 Definition of Done 是什么？
4. 我要补的是 runtime reality，还是只是在补文档？

如果这四个问题回答不清，就不应继续写代码。
