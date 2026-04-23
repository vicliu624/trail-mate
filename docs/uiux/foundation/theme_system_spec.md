# Firmware Theme and Presentation Pack Specification

## 1. Scope

本文定义 Trail Mate 固件的完整界面呈现系统，而不再只把问题理解为“换颜色和图标”。

本文回答以下问题：

- 什么是 `theme`，什么是 `presentation pack`
- 什么属于固件拥有的 `UI semantics`，什么属于第三方可替换的呈现层
- 如果第三方作者希望连布局一起替换，应该依赖哪些稳定接口
- 运行时如何安装、激活、验证、回退和约束主题与 presentation 资源
- 后续固件开发如何避免重新滑回“页面里随手硬写布局和样式”的状态

本文是上位规范。若与其他 UI/UX 文档冲突，以本文为准。

---

## 2. Distinction Baseline

### 2.1 Theme != Presentation Pack

`theme` 是呈现系统的低层对象，负责：

- design token
- component profile
- semantic asset binding

`presentation pack` 是更高层对象，负责：

- 一个 theme 层
- 一个或多个 `layout schema`
- 对不同 `page archetype` 的布局实现

结论：

- 只替换颜色、字体、图标、组件样式时，做的是 `theme-pack`
- 连布局一起替换时，做的是 `presentation-pack`

### 2.2 UI Semantics != Layout Schema

固件拥有页面语义，presentation pack 拥有布局表达。

固件拥有的东西包括：

- 页面职责
- 数据模型
- 用户动作
- 焦点和导航规则
- 允许暴露给 schema 的 component vocabulary

presentation pack 拥有的东西包括：

- 哪些语义 region 出现在页面中
- 这些 region 如何排布
- 每个 region 使用哪些公开 component
- 哪些公开 binding 和 action 被连到哪些 component

### 2.3 Layout Schema != Raw LVGL Object Tree

第三方包不能依赖固件内部 `lv_obj_t*` 层级，也不能假设 child 顺序。

合法依赖对象是：

- `page_archetype.*`
- `region.*`
- `component.*`
- `binding.*`
- `action.*`
- `color.* / font.* / metric.* / motion.*`
- `asset.*`

不合法依赖对象是：

- 某个页面当前第几个 child
- 某个 `.cpp` 里局部变量名
- 某个当前只在一个页面存在的私有 helper

### 2.4 Public Contract != Current Implementation Accident

公共契约必须独立于当前页面实现存在。

因此：

- 文档中声明的 contract ID 是公共接口
- 代码中的 contract catalog 是公共接口
- 页面当前如何组织 LVGL 对象，不是公共接口

### 2.5 Installed != Active != Loaded

主题和 presentation 继续沿用现有 pack 系统的三层区分：

- `installed`
  已解压到存储
- `active`
  当前用户选择的 theme 或 presentation
- `loaded`
  当前真正解析进运行时的数据结构

安装数量不应决定 RAM 常驻占用。

---

## 3. Object Model

### 3.1 Firmware-Owned Objects

固件必须拥有以下对象的定义权和最终解释权：

- `page archetype`
- `region`
- `component vocabulary`
- `binding`
- `action`
- 页面状态和业务逻辑
- 输入、焦点和导航规则
- schema renderer
- schema validator
- fallback presentation

### 3.2 Theme-Owned Objects

theme 层拥有：

- `tokens.ini`
- `components.ini`
- `assets.ini`
- `base_theme`

theme 可以改变：

- 色彩
- 字体
- 间距、圆角、边框
- 动画节奏
- 组件外观
- 语义图标和装饰资源

theme 不能改变：

- 页面结构
- region 组成
- binding 关系
- action 语义

### 3.3 Presentation-Owned Objects

presentation pack 在 theme 之上再拥有：

- `layouts/<page-archetype>.ini`
- `base_presentation`
- 对 archetype 的 layout schema 覆盖

presentation pack 可以改变：

- region 的排布顺序
- region 的显示层级
- 哪些公开 component 用在某个 region
- 公开 binding 如何接到这些 component
- 公开 action 如何接到这些 component

presentation pack 不能改变：

- 业务动作本身
- 业务状态机
- 焦点可达性的最低保障规则
- 固件未公开的私有组件
- 固件未公开的私有数据
- 任意代码执行

### 3.4 Contract Primitives

完整的呈现公共接口由五类稳定对象构成：

- `page_archetype.*`
- `region.*`
- `component.*`
- `binding.*`
- `action.*`

这些对象的代码基线定义在：

- [presentation_contract.h](/C:/Users/VicLi/Documents/Projects/trail-mate/modules/ui_shared/include/ui/presentation/presentation_contract.h)

这些对象的文档清单定义在：

- [presentation_contract_inventory.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/presentation_contract_inventory.md)
- [theme_slot_inventory.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/theme_slot_inventory.md)

### 3.5 Layout Schema

layout schema 是 declarative contract，不是脚本。

v1 schema 必须满足：

- 只引用已公开的 archetype / region / component / binding / action ID
- 只使用固件允许的布局容器语义
- 不包含任意代码
- 不依赖运行时反射或对象树遍历
- 不用自由坐标直接操纵所有对象

---

## 4. Ownership Boundaries

### 4.1 Firmware Owns

- 页面职责
- 页面状态
- 数据来源
- component renderer
- schema parser 和 validator
- fallback chain
- navigation/focus 可达性约束
- 无主题或 schema 失败时的可用界面

### 4.2 Theme Pack Owns

- token
- component profile
- semantic asset binding

### 4.3 Presentation Pack Owns

- theme 层
- archetype 的 layout schema
- 可选的设备 profile / display capability 条件分支

### 4.4 Third-Party Pack Must Not Own

- 任意 C++ 代码
- 任意脚本执行
- 任意 LVGL 树改写
- 绕过固件暴露层访问页面内部状态
- 擅自删除关键动作或破坏可达性

---

## 5. Public Contract Surface

### 5.1 Theme Contract

theme 可依赖的稳定接口包括：

- `color.*`
- `font.*`
- `metric.*`
- `motion.*`
- `component.*`
- `asset.*`

详见：

- [theme_slot_inventory.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/theme_slot_inventory.md)

### 5.2 Presentation Contract

presentation 可依赖的稳定接口包括：

- `page_archetype.*`
- `region.*`
- `component.*`
- `binding.*`
- `action.*`

详见：

- [presentation_contract_inventory.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/presentation_contract_inventory.md)

### 5.3 Code Contract

运行时代码必须以结构化 contract 为唯一真相源，而不是把 Markdown 当成唯一 source of truth。

当前代码基线文件：

- [presentation_contract.h](/C:/Users/VicLi/Documents/Projects/trail-mate/modules/ui_shared/include/ui/presentation/presentation_contract.h)
- [presentation_contract.cpp](/C:/Users/VicLi/Documents/Projects/trail-mate/modules/ui_shared/src/ui/presentation/presentation_contract.cpp)
- [theme_slots.h](/C:/Users/VicLi/Documents/Projects/trail-mate/modules/ui_shared/include/ui/theme/theme_slots.h)
- [theme_slots.cpp](/C:/Users/VicLi/Documents/Projects/trail-mate/modules/ui_shared/src/ui/theme/theme_slots.cpp)

### 5.4 Public Selection Surface vs Builtin Fallback Surface

The runtime now distinguishes between three ID surfaces:

- public selection IDs: `official-*` plus third-party pack IDs
- public builtin baseline ID: `builtin-ascii`
- builtin fallback-only IDs: `builtin-legacy-warm`, `builtin-default`, `builtin-directory-stacked`

Rules:

- Third-party packs may use `builtin-ascii` as the minimal guaranteed builtin baseline.
- Third-party packs should not use `builtin-legacy-warm`, `builtin-default`, or `builtin-directory-stacked` as new authoring targets in manifests, examples, or public docs.
- Firmware may continue to keep those builtin IDs only for compatibility migration, internal fallback, and last-resort recovery.

---

## 6. Package Types

### 6.1 `theme-pack`

`theme-pack` 是较低层的分发对象，只包含：

- token
- component profile
- asset binding

适用场景：

- 只换风格
- 不改页面布局
- 作为 `presentation-pack` 的可继承基础

### 6.2 `presentation-pack`

`presentation-pack` 是更高层分发对象，包含：

- theme 层
- archetype layout schema

适用场景：

- 需要改页面布局
- 需要定义不同 archetype 的布局表达
- 需要作为第三方“完整皮肤包”分发

### 6.3 Transitional Compatibility

当前分支中的运行时代码仍以 `theme` 命名实现 token/asset 层：

- [theme_registry.h](/C:/Users/VicLi/Documents/Projects/trail-mate/modules/ui_shared/include/ui/theme/theme_registry.h)
- [theme_registry.cpp](/C:/Users/VicLi/Documents/Projects/trail-mate/modules/ui_shared/src/ui/theme/theme_registry.cpp)

这是一层过渡实现，不代表最终系统边界仍然只有 theme。

同样地，当前仓库里的 `two_pane_layout.*` / `two_pane_styles.*` 只是内建 presentation 模板文件名。
它们属于 presentation implementation，不应被理解为 firmware-owned semantic object。

当前状态应理解为：

- theme 层已有基础骨架
- presentation contract 已有代码定义
- external `theme-pack` runtime is active
- external `presentation-pack` runtime is active
- `components.ini` is now consumed for a first shared-component batch (`top_bar`, `directory_browser`, `info_card`, `busy_overlay`, `system_notification`)
- layout variation depth is still archetype-dependent, and `directory_browser` remains the most mature multi-variant runtime path

---

## 7. Package Layout

### 7.1 Theme Pack Source Layout

```text
<bundle-root>/
  package.ini
  DESCRIPTION.txt
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

### 7.2 Presentation Pack Source Layout

```text
<bundle-root>/
  package.ini
  DESCRIPTION.txt
  README.md
  presentations/
    <presentation-id>/
      manifest.ini
      tokens.ini
      components.ini
      assets.ini
      layouts/
        menu_dashboard.ini
        chat_conversation.ini
        directory_browser.ini
        map_focus.ini
```

### 7.3 Example `package.ini`

```ini
id=industrial-console
version=1.0.0
package_type=presentation-pack
display_name=Industrial Console
summary=High-density industrial console presentation
description=Full presentation pack with custom layout schemas
author=Example Author
min_firmware_version=0.0.0
supported_memory_profiles=standard,extended
```

### 7.4 Example `manifest.ini`

```ini
kind=presentation
id=industrial-console
api_version=2
base_theme=builtin-ascii
tokens=tokens.ini
components=components.ini
assets=assets.ini
layout.menu_dashboard=layouts/menu_dashboard.ini
layout.chat_conversation=layouts/chat_conversation.ini
layout.directory_browser=layouts/directory_browser.ini
layout.map_focus=layouts/map_focus.ini
```

For a full-coverage standalone pack, omit `base_presentation` rather than pointing at a builtin
official ID. If you intentionally publish an incremental variant on top of the public official
baseline, target `base_presentation=official-default`, not `builtin-default`.

### 7.5 Example `components.ini`

Current theme component profiles use flat keys:

```ini
component.top_bar.container.bg_color=color.accent.primary
component.top_bar.back_button.border_color=color.border.default
component.directory_browser.selector_button.radius=12
component.info_card.header.pad_left=8
component.busy_overlay.progress_bar.accent_color=color.accent.primary
component.notification.banner.shadow_color=#D9B06A
```

Currently supported fields:

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
- `gap` as an alias of `pad_column`
- `pad_left`
- `pad_right`
- `pad_top`
- `pad_bottom`
- `min_height`
- `shadow_width`
- `shadow_opa`
- `bg_opa`

Current runtime-backed shared component batch:

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

This batch now covers shared chrome plus the stable scaffold layer of the public archetypes.
Page-private widget styling is still not universally externalized yet.

This live component batch now covers shared chrome, archetype scaffold surfaces, and the first
page-private extraction batch for tracker / team / GNSS / SSTV / GPS / map.

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

Current theme validator rule in the runtime:

- if a `theme-pack` manifest declares `tokens`, `components`, or `assets`, that file must parse successfully
- unknown theme slot IDs, unknown component keys, invalid values, and missing declared asset files are rejected at load time
- a rejected external theme does not enter the runtime selection surface

### 7.6 Example `layouts/directory_browser.ini`

Current `directory_browser` presentation runtime now accepts a first declarative schema batch.
This batch is intentionally limited to region and selector-button geometry, rather than visual styling.

```ini
archetype=page_archetype.directory_browser
schema_version=v1
layout.axis=row
region.directory.selector.position=first
region.directory.selector.width=80
region.directory.selector.pad_row=3
region.directory.selector.gap_row=3
region.directory.selector.gap_column=3
component.directory.selector_button.height=28
component.directory.selector_button.stacked_min_width=88
component.directory.selector_button.stacked_flex_grow=false
```

Supported `directory_browser` schema keys in the current runtime:

- `archetype=page_archetype.directory_browser`
- `schema_version=v1`
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

Compatibility note:

- legacy `variant=split-sidebar|stacked-selectors` is still accepted
- if both legacy `variant` and `layout.axis` are present, they must agree
- invalid or unknown declarative keys for this archetype must fall back rather than break the device

### 7.7 Example `layouts/chat_conversation.ini` and `layouts/chat_compose.ini`

`chat_conversation` and `chat_compose` are part of the current declarative schema runtime.
Their current runtime surface is intentionally smaller than `directory_browser`, and focuses on
region order, action bar geometry, and action-button sizing.

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

Supported `chat_conversation` schema keys in the current runtime:

- `archetype=page_archetype.chat_conversation`
- `schema_version=v1`
- `layout.row_gap=<int>`
- `region.chat.thread.gap_row=<int>`
- `region.chat.action_bar.position=first|last`
- `region.chat.action_bar.height=<int>`
- `region.chat.action_bar.pad_column=<int>`
- `region.chat.action_bar.align=start|center|end`
- `component.action_button.primary.width=<int>`
- `component.action_button.primary.height=<int>`

Supported `chat_compose` schema keys in the current runtime:

- `archetype=page_archetype.chat_compose`
- `schema_version=v1`
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

### 7.8 Example `layouts/map_focus.ini`

`map_focus` is another active declarative schema batch in the current runtime.
The current runtime surface focuses on archetype scaffold spacing and map-overlay geometry:

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

Supported `map_focus` schema keys in the current runtime:

- `archetype=page_archetype.map_focus`
- `schema_version=v1`
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

### 7.9 Example `layouts/instrument_panel.ini` and `layouts/service_panel.ini`

`instrument_panel` and `service_panel` now also have a first declarative schema batch.
This batch focuses on archetype scaffold geometry, not page-private internal widgets:

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

Supported `instrument_panel` schema keys in the current runtime:

- `archetype=page_archetype.instrument_panel`
- `schema_version=v1`
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

Supported `service_panel` schema keys in the current runtime:

- `archetype=page_archetype.service_panel`
- `schema_version=v1`
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

### 7.10 Example `layouts/menu_dashboard.ini`, `layouts/boot_splash.ini`, and `layouts/watch_face.ini`

`menu_dashboard`, `boot_splash`, and `watch_face` now also have a first declarative schema batch.
This batch is intentionally narrow and only exposes archetype scaffold geometry:

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

Supported `menu_dashboard` schema keys in the current runtime:

- `archetype=page_archetype.menu_dashboard`
- `layout=simple-list|launcher-grid`
- `schema_version=v1`
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

Supported `boot_splash` schema keys in the current runtime:

- `archetype=page_archetype.boot_splash`
- `schema_version=v1`
- `region.boot.hero.offset_x=<int>`
- `region.boot.hero.offset_y=<int>`
- `region.boot.log.inset_x=<int>`
- `region.boot.log.bottom_inset=<int>`
- `region.boot.log.align=start|center|end`

Supported `watch_face` schema keys in the current runtime:

- `archetype=page_archetype.watch_face`
- `schema_version=v1`
- `region.watchface.primary.width_pct=<1..100>`
- `region.watchface.primary.height_pct=<1..100>`
- `region.watchface.primary.offset_x=<int>`
- `region.watchface.primary.offset_y=<int>`

Current validator rule in the runtime:

- if a `presentation-pack` manifest declares a `layout.*` file, that file must parse successfully
- malformed declared layout files are rejected at load time
- a rejected external presentation does not enter the runtime selection surface

---

## 8. Layout Schema Rules

### 8.1 Schema Must Be Declarative

schema 描述的是“摆什么”和“怎么摆”，不是“执行什么代码”。

### 8.2 Schema May Use

- `row`
- `column`
- `stack`
- `grid`
- `overlay`
- `scroll`
- alignment / spacing / density / priority

### 8.3 Schema Must Reference Only Public IDs

schema 中出现的 region、component、binding、action 都必须来自公开 contract。

### 8.4 Schema Must Not Use

- 任意脚本
- 任意表达式求值
- 任意绝对坐标系统
- 对页面内部对象树的 child 索引依赖

### 8.5 Required Region Rule

每个 archetype 都有 required region。

validator 必须保证：

- required region 没有缺失
- unknown ID 直接判错
- 不允许的 region 不能被塞进该 archetype

### 8.6 Action Safety Rule

schema 只能绑定已公开 action。

如果某 archetype 的关键 action 没有可达入口，validator 必须拒绝该 schema 或回退到默认 presentation。

---

## 9. Runtime Contracts

### 9.1 Resolution Order

运行时解析顺序：

1. 读取 `display_theme` 和 `display_presentation`
2. 校验包是否已安装
3. 校验 firmware/api/profile/display capability 兼容性
4. 解析 base chain
5. 解析 theme 层
6. 解析 presentation 层
7. 缺失项逐层回退
8. 最终回退到 `builtin-ascii` + `builtin-default`

### 9.2 Activation Rule

presentation 切换后必须允许完整 UI rebuild。

原因：

- 现有页面样式缓存较多
- 现有页面仍有直接搭树逻辑
- 增量热切换很容易残留旧布局状态

### 9.3 Fallback Rule

任何主题或 presentation 失败时必须保证：

- 用户能回到菜单
- 关键页面还能显示
- 关键 action 仍可触发
- 系统不会因为第三方包损坏而不可用

### 9.4 RAM Rule

presentation 系统延续前文已定义的 RAM 约束：

- 安装数量不等于常驻加载数量
- 同一时刻只保留一个 active presentation
- schema 元数据和资源路径可常驻
- 大图、动画图和外部资源按需加载

---

## 10. Governance Rules

### 10.1 New Page Rule

任何新页面在引入第三方可替换布局前，必须先完成：

- archetype 归属判断
- region 暴露判断
- component vocabulary 判断
- binding / action 暴露判断

### 10.2 No Shadow Contract Rule

禁止页面私自发明只在本页有效、但未进入公共 contract 的外部布局入口。

### 10.3 Contract Change Rule

任何会影响第三方包的变更都必须显式判断：

- 是新增 contract
- 是弃用 contract
- 还是破坏性改动

### 10.4 Documentation Sync Rule

任何 contract 变更必须同步更新：

- 代码 contract catalog
- `presentation_contract_inventory.md`
- `theme_slot_inventory.md`
- 第三方 authoring guide

---

## 11. Migration Plan

### 11.1 Stage A: Theme Layer Baseline

完成项：

- token / asset slot registry
- settings 中的 theme 选择
- 扩展系统识别 theme 元数据

### 11.2 Stage B: Presentation Contract Baseline

当前阶段目标：

- 建立 archetype / region / component / binding / action contract
- 完成规范与代码同步

### 11.3 Stage C: First Renderer Pilot

首个试点应选 `directory_browser` archetype。

原因：

- 现有页面数量多
- 结构相对统一
- 最容易验证 schema 模型是否合理

### 11.4 Stage D: Official Presentation Pack

把当前暖色工程风格迁成第一个官方 `presentation-pack`，而不是继续硬编码在固件里。

### 11.5 Archetype Rollout Order

从这一版开始，presentation 系统的实现推进必须按 archetype 成组推进，而不是按页面零散补丁推进。

推荐顺序：

1. 先补完 `page_archetype.directory_browser`
2. 再做 `page_archetype.chat_conversation` 和 `page_archetype.chat_compose`
3. 然后做 `page_archetype.map_focus`
4. 再做 `page_archetype.instrument_panel` 和 `page_archetype.service_panel`

原因：

- 同一 archetype 内部如果一部分页面走 presentation contract，另一部分页面继续保留私有布局，会重新制造影子立法
- `directory_browser` 当前已经有第一批运行时试点，先收口这一类，收益最大
- `chat_conversation / chat_compose` 结构相对集中，适合作为第二个 renderer archetype
- `map_focus` 最适合验证“语义区域固定、布局表达可替换”
- `instrument_panel` 和 `service_panel` 异质性更高，应该放在中后段

### 11.6 Definition of Done for One Archetype

某个 archetype 只有同时满足以下条件，才可被视为“已经完成 presentation 解耦”：

- archetype / region / component / binding / action 已进入公共 contract
- 至少有一个 builtin renderer 以 archetype 语义而不是页面私有结构运行
- 同 archetype 下的页面族都已迁到该 renderer 或其语义 facade
- settings / startup / registry 能识别并切换对应 presentation 选择
- 文档已同步标记当前成熟度和第三方可依赖范围

只满足“contract 已写入文档”，不能视为该 archetype 已完成运行时解耦。

---

## 12. Current Status Note

截至当前分支，必须实话实说：

- theme 层代码已有基础骨架
- presentation contract 代码已建立
- external layout schema 解析和渲染已在 `directory_browser`、`chat_conversation`、`chat_compose`、`map_focus`、`instrument_panel`、`service_panel` 进入首批 runtime，但尚未覆盖所有 archetype

更重要的是，当前实现成熟度不是“所有 archetype 都已进入可替换布局运行时”，而是分层状态：

| Archetype | Contract Status | Runtime Status |
| --- | --- | --- |
| `page_archetype.directory_browser` | defined | first declarative schema pilot active |
| `page_archetype.chat_list` | defined | partial runtime alignment only |
| `page_archetype.chat_conversation` | defined | first declarative schema batch active |
| `page_archetype.chat_compose` | defined | first declarative schema batch active |
| `page_archetype.map_focus` | defined | first declarative schema batch active |
| `page_archetype.instrument_panel` | defined | first declarative schema batch active |
| `page_archetype.service_panel` | defined | first declarative schema batch active |
| `page_archetype.menu_dashboard` | defined | first declarative schema batch active |
| `page_archetype.boot_splash` | defined | first declarative schema batch active |
| `page_archetype.watch_face` | defined | first declarative schema batch active |

因此第三方作者此刻能稳定依赖的是：

- theme slot contract
- presentation contract ID catalog

而真正完整的 `presentation-pack` 运行时能力，依然是接下来实现阶段的目标；当前已经落地的是 `directory_browser + chat + map_focus + instrument/service + menu/boot/watch` 的首批 declarative schema runtime，而不是所有 archetype 都已等价成熟。

换句话说：

- 现有规格不是方向错误，而是“规范目标已经铺开，运行时完成度还远未覆盖所有 archetype”
- 后续工作必须把每个 archetype 的实现成熟度显式写清，而不是让“已定义 contract”被误读成“已完成全页面解耦”
