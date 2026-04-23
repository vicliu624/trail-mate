# Third-Party Presentation Pack Authoring Guide

## 1. Scope

本文面向第三方视觉作者和插件作者，说明如何为 Trail Mate 开发：

- `theme-pack`
- `presentation-pack`

本文优先解决作者最关心的问题：

- 我到底能改什么，不能改什么
- 什么时候只做 theme，什么时候必须做 presentation
- 如果要替换布局，我应该依赖哪些稳定接口
- 包结构、schema、资源和 RAM 应该怎么约束

若本文与以下文档冲突，以下文档优先：

- [theme_system_spec.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/theme_system_spec.md)
- [theme_slot_inventory.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/theme_slot_inventory.md)
- [presentation_contract_inventory.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/presentation_contract_inventory.md)

---

## 2. First Decision: Theme or Presentation

### 2.1 When to Build a `theme-pack`

满足以下条件时，做 `theme-pack`：

- 你只想换颜色、字体、圆角、边框、动画节奏
- 你只想换语义图标、logo、装饰资源
- 你不想改页面布局

### 2.2 When to Build a `presentation-pack`

满足以下任一条件时，做 `presentation-pack`：

- 你希望重排页面布局
- 你希望同一 archetype 采用不同的 panel 分布
- 你希望把同样的语义元素组织成另一种结构

### 2.3 A Practical Rule

如果你问自己“我能不能把这个按钮移到另一块区域里”，那你已经不在做 theme 了，而是在做 presentation。

---

## 3. First Principle

你写的不是“另一套固件页面”，而是“固件页面语义的外部呈现实现”。

请始终记住下面四条边界：

1. 固件拥有页面职责、状态、导航、焦点和动作。
2. theme 拥有 token、component profile、semantic asset binding。
3. presentation 拥有布局 schema。
4. 第三方包不能拥有任意代码执行权。

---

## 4. What You Can Change

### 4.1 Theme-Pack Can Change

- `color.*`
- `font.*`
- `metric.*`
- `motion.*`
- `component.*`
- `asset.*`

### 4.2 Presentation-Pack Can Also Change

- `page_archetype.*` 的布局 schema
- `region.*` 的排布方式
- 某个 region 中使用哪些公开 component
- 公开 binding 和 action 的连接关系

### 4.3 Typical Allowed Examples

- 把整体风格做成工业仪表、纸质导航、极简 ASCII、明亮户外风格
- 替换主菜单 app icon、顶部状态 icon、启动 logo
- 把 `directory_browser` 从“左侧选择器 + 右侧内容”改成“上部选择器 + 下部内容”
- 把 `chat_conversation` 改成更强调 thread 信息密度的结构

---

## 5. What You Cannot Change

- 页面业务逻辑
- 页面状态机
- 页面路由关系
- 页面焦点最低保障规则
- 固件未公开的私有对象
- 固件未公开的私有数据
- 任意脚本执行
- 任意 C++ 注入
- 依赖 LVGL child 顺序

### 5.1 Typical Forbidden Examples

- 在包里发明一个新的聊天业务流程
- 通过布局把关键返回动作彻底藏掉
- 直接依赖某个 `.cpp` 里叫 `btn3` 的对象
- 在 schema 里写自定义脚本

---

## 6. Public Contracts You Must Follow

第三方作者只能依赖以下公开契约：

- [theme_slot_inventory.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/theme_slot_inventory.md)
- [presentation_contract_inventory.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/presentation_contract_inventory.md)
- [presentation_contract.h](/C:/Users/VicLi/Documents/Projects/trail-mate/modules/ui_shared/include/ui/presentation/presentation_contract.h)

### 6.1 Public IDs and Builtin IDs

When you choose package IDs, base chains, and tutorial examples, use this rule:

- public selection IDs: `official-*` and your own third-party IDs
- public builtin baseline: `builtin-ascii`
- builtin fallback-only IDs: `builtin-legacy-warm`, `builtin-default`, `builtin-directory-stacked`

That means:

- `base_theme=builtin-ascii` is valid and recommended for a minimal guaranteed baseline
- `base_presentation=official-default` is valid when you intentionally derive from the public official presentation baseline
- do not publish new packs or guides that tell authors to target `builtin-default`, `builtin-directory-stacked`, or `builtin-legacy-warm`

当前仓库中的 `two_pane_layout.*` / `two_pane_styles.*` 只是官方内建 presentation 的实现文件名，
不是你应该直接依赖的 public semantic contract。

请不要依赖以下内容：

- 某个页面当前的 LVGL 对象树
- 某个页面内部 helper 函数
- 资源的当前静态变量名
- 当前某个 child index

---

## 7. Authoring Levels

### 7.1 Level 1: Minimal Theme Pack

目标：

- 包可安装、可识别、可切换
- 整体视觉明显变化
- 不引入额外布局风险

最少覆盖：

- `tokens.ini`
- `components.ini`

### 7.2 Level 2: Standard Theme Pack

目标：

- 共享 chrome 和共享组件统一
- 图标和 logo 也完成替换

新增覆盖：

- `assets.ini`
- 共享 widget/component slot

### 7.3 Level 3: Full Presentation Pack

目标：

- 不只换皮，而是换布局
- 至少一个 archetype 的 schema 能完整工作

新增覆盖：

- `layouts/<page-archetype>.ini`

---

## 8. Recommended Workflow

建议按以下顺序工作：

1. 先确定目标设备 profile。
2. 先做 theme 层，不要一上来就做完整布局。
3. 先在一个 archetype 上验证 schema。
4. 验证 fallback 后，再扩到其他 archetype。
5. 最后再加大资源体积。

这样做的好处是：

- 你总有一个可运行版本
- 更容易分辨“这件事是 token 问题还是 layout 问题”
- 更容易在 RAM 预算内迭代

---

## 9. Package Layout

### 9.1 Theme Pack

```text
<bundle-root>/
  package.ini
  README.md
  themes/
    <theme-id>/
      manifest.ini
      tokens.ini
      components.ini
      assets.ini
      assets/
        ...
```

### 9.2 Presentation Pack

```text
<bundle-root>/
  package.ini
  README.md
  presentations/
    <presentation-id>/
      manifest.ini
      tokens.ini
      components.ini
      assets.ini
      layouts/
        menu_dashboard.ini
        chat_list.ini
        chat_conversation.ini
        chat_compose.ini
        directory_browser.ini
        map_focus.ini
```

---

## 10. Required Files

### 10.1 `package.ini`

theme pack 示例：

```ini
id=paper-nav
version=1.0.0
package_type=theme-pack
display_name=Paper Nav
summary=Daylight paper-like theme
min_firmware_version=0.0.0
```

presentation pack 示例：

```ini
id=industrial-console
version=1.0.0
package_type=presentation-pack
display_name=Industrial Console
summary=Theme plus custom layout schemas
min_firmware_version=0.0.0
```

### 10.2 `manifest.ini`

theme 示例：

```ini
kind=theme
id=paper-nav
api_version=2
base_theme=builtin-ascii
tokens=tokens.ini
components=components.ini
assets=assets.ini
```

presentation 示例：

```ini
kind=presentation
id=industrial-console
api_version=2
base_theme=builtin-ascii
tokens=tokens.ini
components=components.ini
assets=assets.ini
layout.menu_dashboard=layouts/menu_dashboard.ini
layout.directory_browser=layouts/directory_browser.ini
```

If your pack fully declares the archetypes it needs, omit `base_presentation`.
If you are intentionally publishing an override pack, derive from `official-default`,
not from `builtin-default`.

### 10.3 `components.ini`

Current runtime expects flat component keys:

```ini
component.top_bar.container.bg_color=color.accent.primary
component.top_bar.back_button.border_width=1
component.directory_browser.selector_button.radius=12
component.info_card.base.border_color=color.border.default
component.busy_overlay.progress_bar.accent_color=color.accent.primary
component.notification.banner.shadow_color=#D9B06A
```

Supported fields today:

- `bg_color`
- `border_color`
- `text_color`
- `accent_color`
- `shadow_color`
- `border_width`
- `radius`
- `pad_all`
- `pad_row`
- `pad_column`
- `gap`
- `pad_left`
- `pad_right`
- `pad_top`
- `pad_bottom`
- `min_height`
- `shadow_width`
- `shadow_opa`
- `bg_opa`

Current runtime-backed component slots:

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

Anything outside this batch should still be treated as future surface, not as guaranteed live runtime behavior.
This batch now covers shared chrome, archetype scaffold surfaces, and the first page-private
extraction batch for tracker / team / GNSS / SSTV / GPS / map.

Current live semantic color extraction batch also includes:

- `color.map.route`
- `color.map.track`
- `color.map.marker.border`
- `color.map.signal_label.bg`
- `color.map.node_marker`
- `color.map.self_marker`
- `color.map.link`
- `color.map.readout.*`
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

Current theme validator rule:

- if your manifest declares `tokens=...`, `components=...`, or `assets=...`, that file must parse successfully
- unknown theme slot IDs, unknown component keys, invalid values, and missing declared asset files are rejected at load time
- a rejected external theme will not appear in the runtime selection list

### 10.4 `layouts/directory_browser.ini`

`page_archetype.directory_browser` is now the first archetype with a real declarative
presentation runtime batch. Current files should use public IDs and geometry-only layout fields:

```ini
archetype=page_archetype.directory_browser
schema_version=v1
layout.axis=column
region.directory.selector.position=first
region.directory.selector.pad_row=3
region.directory.selector.gap_row=3
region.directory.selector.gap_column=3
component.directory.selector_button.height=28
component.directory.selector_button.stacked_min_width=88
component.directory.selector_button.stacked_flex_grow=false
```

Supported keys today:

- `layout.axis=row|column`
- `region.directory.selector.position=first|last`
- `region.directory.selector.width=<int>`
- `region.directory.selector.pad_all=<int>`
- `region.directory.selector.pad_row=<int>`
- `region.directory.selector.gap_row=<int>`
- `region.directory.selector.gap_column=<int>`
- `region.directory.selector.margin_after=<int>`
- `region.directory.content.pad_all=<int>`
- `region.directory.content.pad_row=<int>`
- `region.directory.content.pad_left=<int>`
- `region.directory.content.pad_right=<int>`
- `region.directory.content.pad_top=<int>`
- `region.directory.content.pad_bottom=<int>`
- `component.directory.selector_button.height=<int>`
- `component.directory.selector_button.stacked_min_width=<int>`
- `component.directory.selector_button.stacked_flex_grow=true|false`

Notes:

- current runtime still accepts legacy `variant=split-sidebar|stacked-selectors`
- when both legacy `variant` and `layout.axis` appear, they must agree
- this v1 batch controls layout geometry only; color, border, and asset styling still belong to `theme-pack`

### 10.5 `layouts/chat_conversation.ini` and `layouts/chat_compose.ini`

`page_archetype.chat_conversation` and `page_archetype.chat_compose` now have a first
declarative schema batch too. This batch focuses on region order and action-area geometry:

```ini
# chat_conversation
archetype=page_archetype.chat_conversation
schema_version=v1
region.chat.action_bar.position=last
region.chat.action_bar.align=center

# chat_compose
archetype=page_archetype.chat_compose
schema_version=v1
region.chat.action_bar.position=last
region.chat.action_bar.align=start
```

Supported `chat_conversation` keys today:

- `layout.row_gap=<int>`
- `region.chat.thread.gap_row=<int>`
- `region.chat.action_bar.position=first|last`
- `region.chat.action_bar.height=<int>`
- `region.chat.action_bar.pad_column=<int>`
- `region.chat.action_bar.align=start|center|end`
- `component.action_button.primary.width=<int>`
- `component.action_button.primary.height=<int>`

Supported `chat_compose` keys today:

- `layout.row_gap=<int>`
- `region.chat.composer.pad_all=<int>`
- `region.chat.composer.pad_row=<int>`
- `region.chat.action_bar.position=first|last`
- `region.chat.action_bar.height=<int>`
- `region.chat.action_bar.pad_left=<int>`
- `region.chat.action_bar.pad_right=<int>`
- `region.chat.action_bar.pad_top=<int>`
- `region.chat.action_bar.pad_bottom=<int>`
- `region.chat.action_bar.pad_column=<int>`
- `region.chat.action_bar.align=start|center|end`
- `component.action_button.primary.width=<int>`
- `component.action_button.primary.height=<int>`
- `component.action_button.secondary.width=<int>`
- `component.action_button.secondary.height=<int>`

### 10.6 `layouts/map_focus.ini`

`page_archetype.map_focus` now has a first declarative schema batch too. This batch
focuses on archetype scaffold spacing and map-overlay geometry:

```ini
archetype=page_archetype.map_focus
schema_version=v1
layout.row_gap=0
region.map.overlay.primary.position=top_right
region.map.overlay.primary.width=85
region.map.overlay.primary.offset_y=3
region.map.overlay.primary.gap_row=3
region.map.overlay.primary.flow=column
region.map.overlay.secondary.position=top_left
region.map.overlay.secondary.width=90
region.map.overlay.secondary.offset_y=3
region.map.overlay.secondary.gap_row=3
region.map.overlay.secondary.flow=column
```

Supported `map_focus` keys today:

- `layout.row_gap=<int>`
- `region.map.overlay.primary.position=top_left|top_right|bottom_left|bottom_right`
- `region.map.overlay.primary.width=<int>`
- `region.map.overlay.primary.offset_x=<int>`
- `region.map.overlay.primary.offset_y=<int>`
- `region.map.overlay.primary.gap_row=<int>`
- `region.map.overlay.primary.gap_column=<int>`
- `region.map.overlay.primary.flow=row|column`
- `region.map.overlay.secondary.position=top_left|top_right|bottom_left|bottom_right`
- `region.map.overlay.secondary.width=<int>`
- `region.map.overlay.secondary.offset_x=<int>`
- `region.map.overlay.secondary.offset_y=<int>`
- `region.map.overlay.secondary.gap_row=<int>`
- `region.map.overlay.secondary.gap_column=<int>`
- `region.map.overlay.secondary.flow=row|column`

### 10.7 `layouts/instrument_panel.ini` and `layouts/service_panel.ini`

`page_archetype.instrument_panel` and `page_archetype.service_panel` now also have a
first declarative schema batch. This batch focuses on archetype scaffold geometry:

```ini
# instrument_panel
archetype=page_archetype.instrument_panel
schema_version=v1
layout.body.flow=column
region.instrument.canvas.pad_all=0

# service_panel
archetype=page_archetype.service_panel
schema_version=v1
layout.body.pad_all=0
region.service.primary_panel.pad_all=0
region.service.footer_actions.pad_column=0
```

Supported `instrument_panel` keys today:

- `layout.row_gap=<int>`
- `layout.body.flow=row|column`
- `layout.body.pad_all=<int>`
- `layout.body.pad_row=<int>`
- `layout.body.pad_column=<int>`
- `region.instrument.canvas.grow=true|false`
- `region.instrument.canvas.pad_all=<int>`
- `region.instrument.canvas.pad_row=<int>`
- `region.instrument.canvas.pad_column=<int>`
- `region.instrument.legend.grow=true|false`
- `region.instrument.legend.pad_all=<int>`
- `region.instrument.legend.pad_row=<int>`
- `region.instrument.legend.pad_column=<int>`

Supported `service_panel` keys today:

- `layout.row_gap=<int>`
- `layout.body.pad_all=<int>`
- `layout.body.pad_row=<int>`
- `layout.body.pad_column=<int>`
- `region.service.primary_panel.pad_all=<int>`
- `region.service.primary_panel.pad_row=<int>`
- `region.service.primary_panel.pad_column=<int>`
- `region.service.footer_actions.height=<int>`
- `region.service.footer_actions.pad_all=<int>`
- `region.service.footer_actions.pad_row=<int>`
- `region.service.footer_actions.pad_column=<int>`

### 10.8 `layouts/menu_dashboard.ini`, `layouts/boot_splash.ini`, and `layouts/watch_face.ini`

`page_archetype.menu_dashboard`, `page_archetype.boot_splash`, and `page_archetype.watch_face`
now also have a first declarative schema batch. This batch is intentionally limited to archetype
scaffold geometry:

```ini
# menu_dashboard
archetype=page_archetype.menu_dashboard
layout=launcher-grid
schema_version=v1
region.menu.app_grid.height_pct=70
region.menu.app_grid.top_offset=30
region.menu.app_grid.align=center
region.menu.bottom_chips.height=30

# boot_splash
archetype=page_archetype.boot_splash
schema_version=v1
region.boot.hero.offset_y=0
region.boot.log.inset_x=10
region.boot.log.bottom_inset=8
region.boot.log.align=start

# watch_face
archetype=page_archetype.watch_face
schema_version=v1
region.watchface.primary.width_pct=100
region.watchface.primary.height_pct=100
region.watchface.primary.offset_x=0
region.watchface.primary.offset_y=0
```

Supported `menu_dashboard` keys today:

- `layout=simple-list|launcher-grid`
- `region.menu.app_grid.height_pct=<1..100>`
- `region.menu.app_grid.top_offset=<int>`
- `region.menu.app_grid.pad_row=<int>`
- `region.menu.app_grid.pad_column=<int>`
- `region.menu.app_grid.pad_left=<int>`
- `region.menu.app_grid.pad_right=<int>`
- `region.menu.app_grid.align=start|center|end`
- `region.menu.bottom_chips.height=<int>`
- `region.menu.bottom_chips.pad_left=<int>`
- `region.menu.bottom_chips.pad_right=<int>`
- `region.menu.bottom_chips.pad_column=<int>`

Supported `boot_splash` keys today:

- `region.boot.hero.offset_x=<int>`
- `region.boot.hero.offset_y=<int>`
- `region.boot.log.inset_x=<int>`
- `region.boot.log.bottom_inset=<int>`
- `region.boot.log.align=start|center|end`

Supported `watch_face` keys today:

- `region.watchface.primary.width_pct=<1..100>`
- `region.watchface.primary.height_pct=<1..100>`
- `region.watchface.primary.offset_x=<int>`
- `region.watchface.primary.offset_y=<int>`

Current validator rule:

- if your manifest declares `layout.<archetype>=...`, that file must parse successfully
- malformed declared layout files are rejected at load time
- a rejected external presentation will not appear in the runtime selection list

---

## 11. Layout Schema Rules

### 11.1 Schema Is Declarative

你写的是结构描述，不是脚本。

### 11.2 Schema May Reference

- `page_archetype.*`
- `region.*`
- `component.*`
- `binding.*`
- `action.*`

### 11.3 Schema Must Not Reference

- 未公开 ID
- 私有对象名
- 绝对页面坐标
- 自定义表达式解释器

### 11.4 Schema Must Preserve Required Regions

如果某 archetype 的 required region 缺失，schema 应被视为无效。

### 11.5 Schema Must Preserve Action Reachability

关键动作必须仍然可达。  
如果布局让 `back`、`open`、`send`、`install` 这类关键动作失去入口，schema 应被拒绝或回退。

---

## 12. Current Archetype Entry Points

当前最值得第三方作者关注的 archetype：

- `page_archetype.menu_dashboard`
- `page_archetype.boot_splash`
- `page_archetype.watch_face`
- `page_archetype.chat_list`
- `page_archetype.chat_conversation`
- `page_archetype.chat_compose`
- `page_archetype.directory_browser`
- `page_archetype.map_focus`
- `page_archetype.instrument_panel`
- `page_archetype.service_panel`

其中最推荐先做试点的是：

- `page_archetype.directory_browser`

原因：

- Settings / Contacts / Extensions / Tracker 这一类页面结构最接近
- 最容易验证一套通用 schema 是否足够表达

---

## 13. RAM and Resource Advice

第三方作者最容易失控的地方不是 token，而是资源。

建议：

- 先用小资源验证布局，再逐步增加图片资源
- 避免大 GIF 和不必要的大 PNG
- 尽量让同一类 icon 共享视觉规则
- 不要假设安装多个包会同时加载进 RAM

---

## 14. Validation Checklist

发布前至少检查：

- 所有引用的 contract ID 都已公开
- theme slot 引用都存在
- layout schema 不缺 required region
- 关键 action 可达
- 缺失资源时仍能回退
- 目标设备 profile 下 RAM 和显示效果可接受

---

## 15. Current Runtime Status

当前这条分支必须诚实说明：

- theme 层已有运行时代码骨架
- presentation contract 已进入代码
- external `theme-pack` runtime is real and installable today
- external `presentation-pack` runtime is real and installable today
- `components.ini` is already live for a first shared-component batch: `top_bar`, `directory_browser`, `info_card`, `busy_overlay`, `system_notification`
- `page_archetype.directory_browser` now has the first declarative schema runtime batch on top of the most mature multi-variant path
- `page_archetype.chat_conversation` and `page_archetype.chat_compose` now have a first declarative schema batch for action-region ordering and geometry
- `page_archetype.map_focus` now has a first declarative schema batch for overlay-region geometry
- `page_archetype.instrument_panel` and `page_archetype.service_panel` now have a first declarative schema batch for scaffold geometry
- `page_archetype.menu_dashboard`, `page_archetype.boot_splash`, and `page_archetype.watch_face` now have a first declarative schema batch for display-surface geometry

同时必须区分两件事：

- 本指南描述的是第三方长期应当依赖的规范目标
- 当前固件运行时只完成了部分 archetype 的试点接入

当前 archetype 成熟度应理解为：

| Archetype | Third-Party Contract | Current Runtime |
| --- | --- | --- |
| `page_archetype.directory_browser` | stable target | first declarative schema pilot |
| `page_archetype.chat_list` | stable target | partial runtime alignment |
| `page_archetype.chat_conversation` | stable target | first declarative schema batch active |
| `page_archetype.chat_compose` | stable target | first declarative schema batch active |
| `page_archetype.map_focus` | stable target | first declarative schema batch active |
| `page_archetype.instrument_panel` | stable target | first declarative schema batch active |
| `page_archetype.service_panel` | stable target | first declarative schema batch active |
| `page_archetype.menu_dashboard` | stable target | first declarative schema batch active |
| `page_archetype.boot_splash` | stable target | first declarative schema batch active |
| `page_archetype.watch_face` | stable target | first declarative schema batch active |

这意味着：

- 你现在可以稳定围绕 contract 设计第三方包
- 但真正完整的“第三方布局替换运行时”还不是已经完成的事实

还意味着：

- 如果你现在就要做最稳的第三方试点，请优先围绕 `page_archetype.directory_browser`
- 其他 archetype 目前更适合作为 contract 对齐和资源设计参考，而不是立即假定已有完整 renderer 支撑

如果你要给未来的 Trail Mate 做长期可维护的第三方包，请以 contract 和本指南为准，而不是以当前页面内部实现细节为准。
