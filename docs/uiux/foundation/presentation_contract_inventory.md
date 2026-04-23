# Presentation Contract Inventory

## 1. Purpose

本文是 Trail Mate 布局可替换体系的公共 contract 目录。

它解决的问题不是“当前页面长什么样”，而是：

- 第三方 presentation 作者到底可以摆哪些语义对象
- 固件开发者在重构页面时，哪些 ID 不能随意改
- layout schema 可以引用哪些 archetype / region / component / binding / action

本文与 [theme_slot_inventory.md](/C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/foundation/theme_slot_inventory.md) 的关系是：

- `theme_slot_inventory.md` 管 token / component profile / asset 这一层
- 本文管布局 contract 这一层

---

## 2. Contract Rules

### 2.1 Inventory Covers Semantic Surface

这里列的是 public contract，不是 LVGL 对象树快照。

### 2.2 Stable IDs Are Mandatory

一旦某个 ID 进入本文，它就不再是页面私有实现细节。

### 2.3 Third-Party Layouts Must Depend on This File

presentation pack 只能引用本文声明的 ID，不能自己发明新的公共 ID。

---

## 3. Normative Code Source

本文件的代码基线是：

- [presentation_contract.h](/C:/Users/VicLi/Documents/Projects/trail-mate/modules/ui_shared/include/ui/presentation/presentation_contract.h)
- [presentation_contract.cpp](/C:/Users/VicLi/Documents/Projects/trail-mate/modules/ui_shared/src/ui/presentation/presentation_contract.cpp)

需要特别区分：

- `two_pane_layout.*` / `two_pane_styles.*` 是当前内建 presentation 的一种实现
- 它们不是 firmware semantic contract 本体

---

## 4. Page Archetypes

| ID | Current Meaning | Current Coverage |
| --- | --- | --- |
| `page_archetype.menu_dashboard` | 启动后的 launcher / dashboard 主表面 | `menu/*`, `menu_dashboard.cpp` |
| `page_archetype.boot_splash` | 启动画面和早期日志 | `ui_boot.cpp` |
| `page_archetype.watch_face` | 待机式 glanceable 表面 | `watch_face.cpp` |
| `page_archetype.chat_list` | 会话列表 | `screens/chat/chat_message_list_*` |
| `page_archetype.chat_conversation` | 会话线程 | `screens/chat/chat_conversation_*` |
| `page_archetype.chat_compose` | 输入和 IME 宿主页 | `screens/chat/chat_compose_*` |
| `page_archetype.directory_browser` | 选择器驱动的目录/详情页面族 | `settings`, `contacts`, `extensions`, `tracker` |
| `page_archetype.map_focus` | 地图中心型页面 | `gps`, `node_info` |
| `page_archetype.instrument_panel` | 仪表和可视化页 | `energy_sweep`, `gnss`, `sstv`, `walkie_talkie` |
| `page_archetype.service_panel` | 工具和服务页 | `pc_link`, `usb`, `team` |

---

## 5. Region Slots

| ID | Meaning |
| --- | --- |
| `region.chrome.top_bar` | 共享页头 |
| `region.chrome.status_row` | 共享状态图标行 |
| `region.chrome.footer` | 页脚或次级 chrome |
| `region.menu.app_grid` | 主菜单 app grid / list |
| `region.menu.bottom_chips` | 主菜单底部 chip 区 |
| `region.boot.hero` | 启动 logo / hero 区 |
| `region.boot.log` | 启动日志区 |
| `region.watchface.primary` | 待机主展示区 |
| `region.chat.thread` | 会话列表或消息线程主体 |
| `region.chat.composer` | 输入器和 IME 宿主区 |
| `region.chat.action_bar` | 聊天动作条 |
| `region.directory.header` | 目录页头与上下文控制区 |
| `region.directory.selector` | 分类/模式/筛选等选择器区域 |
| `region.directory.content` | 条目和详情内容区域 |
| `region.map.viewport` | 地图主视口 |
| `region.map.overlay.primary` | 地图一级 overlay |
| `region.map.overlay.secondary` | 地图二级 overlay |
| `region.instrument.canvas` | 主图表 / 仪表画布 |
| `region.instrument.legend` | 图例和次级指标区 |
| `region.service.primary_panel` | 工具页主 panel |
| `region.service.footer_actions` | 工具页底部动作区 |

---

## 6. Region Support by Archetype

### 6.1 `page_archetype.menu_dashboard`

| Region | Required | Repeatable |
| --- | --- | --- |
| `region.chrome.top_bar` | yes | no |
| `region.chrome.status_row` | no | no |
| `region.menu.app_grid` | yes | no |
| `region.menu.bottom_chips` | no | yes |

当前 runtime 说明：

- 第一批 declarative schema 已接入 `region.menu.app_grid` 与 `region.menu.bottom_chips` 的 scaffold geometry
- 菜单按钮内容、说明文字与 dashboard 私有卡片结构仍由固件拥有

### 6.2 `page_archetype.boot_splash`

| Region | Required | Repeatable |
| --- | --- | --- |
| `region.boot.hero` | yes | no |
| `region.boot.log` | yes | yes |

当前 runtime 说明：

- 第一批 declarative schema 已接入 `region.boot.hero` 与 `region.boot.log` 的几何
- 启动时序、logo 资源选择与日志内容仍由固件拥有

### 6.3 `page_archetype.watch_face`

| Region | Required | Repeatable |
| --- | --- | --- |
| `region.watchface.primary` | yes | no |
| `region.chrome.footer` | no | no |

当前 runtime 说明：

- 第一批 declarative schema 已接入 `region.watchface.primary` 的容器几何
- 表盘内部 hour / minute / date / battery / node-id 的具体排版仍由固件拥有

### 6.4 `page_archetype.chat_list`

| Region | Required | Repeatable |
| --- | --- | --- |
| `region.chrome.top_bar` | yes | no |
| `region.chat.thread` | yes | yes |
| `region.chrome.footer` | no | no |

### 6.5 `page_archetype.chat_conversation`

| Region | Required | Repeatable |
| --- | --- | --- |
| `region.chrome.top_bar` | yes | no |
| `region.chat.thread` | yes | yes |
| `region.chat.action_bar` | yes | no |

### 6.6 `page_archetype.chat_compose`

| Region | Required | Repeatable |
| --- | --- | --- |
| `region.chrome.top_bar` | yes | no |
| `region.chat.composer` | yes | no |
| `region.chat.action_bar` | yes | no |

### 6.7 `page_archetype.directory_browser`

| Region | Required | Repeatable |
| --- | --- | --- |
| `region.chrome.top_bar` | yes | no |
| `region.directory.header` | yes | no |
| `region.directory.selector` | yes | yes |
| `region.directory.content` | yes | yes |

### 6.8 `page_archetype.map_focus`

| Region | Required | Repeatable |
| --- | --- | --- |
| `region.chrome.top_bar` | yes | no |
| `region.map.viewport` | yes | no |
| `region.map.overlay.primary` | yes | yes |
| `region.map.overlay.secondary` | no | yes |

### 6.9 `page_archetype.instrument_panel`

| Region | Required | Repeatable |
| --- | --- | --- |
| `region.chrome.top_bar` | yes | no |
| `region.instrument.canvas` | yes | no |
| `region.instrument.legend` | no | yes |

### 6.10 `page_archetype.service_panel`

| Region | Required | Repeatable |
| --- | --- | --- |
| `region.chrome.top_bar` | yes | no |
| `region.service.primary_panel` | yes | no |
| `region.service.footer_actions` | no | no |

---

## 7. Component Vocabulary

| ID | Meaning |
| --- | --- |
| `component.top_bar.container` | 顶部条容器 |
| `component.top_bar.back_button` | 返回按钮 |
| `component.top_bar.title_label` | 顶部主标题 |
| `component.top_bar.right_label` | 顶部右侧 meta 文本 |
| `component.status.icon_strip` | 状态图标条 |
| `component.menu.app_button` | 菜单 app 按钮 |
| `component.menu.bottom_chip` | 菜单底部 chip |
| `component.modal.panel` | 模态面板 |
| `component.notification.banner` | 系统通知条 |
| `component.toast.banner` | toast 表面 |
| `component.directory.selector_button` | 目录选择器按钮 |
| `component.directory.entry_item` | 目录内容条目 |
| `component.info_card.base` | 信息卡片 |
| `component.chat.list_item` | 会话列表项 |
| `component.chat.bubble.self` | 自己的消息气泡 |
| `component.chat.bubble.peer` | 对方消息气泡 |
| `component.chat.composer.editor` | 输入编辑区 |
| `component.action_button.primary` | 主动作按钮 |
| `component.action_button.secondary` | 次动作按钮 |
| `component.status_chip` | 状态 chip |

---

## 8. Binding Slots

| ID | Collection | Meaning |
| --- | --- | --- |
| `binding.screen.title` | no | 页面标题文本 |
| `binding.screen.subtitle` | no | 页面副标题或 meta |
| `binding.status.summary` | no | 状态摘要 |
| `binding.menu.app_entries` | yes | 菜单 app 条目 |
| `binding.menu.bottom_summary` | no | 菜单底部汇总 |
| `binding.boot.log_lines` | yes | 启动日志行 |
| `binding.chat.thread_items` | yes | 聊天消息或会话条目 |
| `binding.chat.draft_text` | no | 当前输入草稿 |
| `binding.directory.selectors` | yes | 目录选择器集合 |
| `binding.directory.entries` | yes | 二栏内容条目 |
| `binding.directory.detail_document` | no | 选中项详情 |
| `binding.map.viewport_model` | no | 地图视口模型 |
| `binding.map.overlay_summary` | no | 地图 overlay 摘要 |
| `binding.instrument.primary_series` | yes | 仪表主数据序列 |
| `binding.instrument.legend_items` | yes | 仪表图例项 |
| `binding.service.body_text` | no | 工具页主体文本 |
| `binding.package_catalog_entries` | yes | 包目录条目 |
| `binding.settings.section_entries` | yes | 设置 section/row 集合 |

---

## 9. Action Slots

| ID | User Visible | Meaning |
| --- | --- | --- |
| `action.navigate.back` | yes | 返回上一页 |
| `action.navigate.open` | yes | 打开当前焦点项 |
| `action.navigate.next` | no | 焦点向前 |
| `action.navigate.previous` | no | 焦点向后 |
| `action.activate.primary` | yes | 页面主动作 |
| `action.activate.secondary` | yes | 页面次动作 |
| `action.refresh.content` | yes | 刷新内容 |
| `action.package.install` | yes | 安装包 |
| `action.package.remove` | yes | 移除包 |
| `action.presentation.apply` | yes | 应用 presentation / theme |
| `action.message.send` | yes | 发送消息 |
| `action.directory.toggle_selector` | yes | 切换目录选择器区域 |
| `action.map.zoom_in` | yes | 地图放大 |
| `action.map.zoom_out` | yes | 地图缩小 |

---

## 10. Mapping Current Pages to Archetypes

| Current Page Family | Target Archetype |
| --- | --- |
| Menu / Dashboard | `page_archetype.menu_dashboard` |
| Boot | `page_archetype.boot_splash` |
| Watch Face | `page_archetype.watch_face` |
| Chat List | `page_archetype.chat_list` |
| Chat Conversation | `page_archetype.chat_conversation` |
| Chat Compose | `page_archetype.chat_compose` |
| Contacts | `page_archetype.directory_browser` |
| Settings | `page_archetype.directory_browser` |
| Extensions | `page_archetype.directory_browser` |
| Team | `page_archetype.service_panel` |
| Tracker | `page_archetype.directory_browser` |
| GPS | `page_archetype.map_focus` |
| Node Info | `page_archetype.map_focus` |
| Energy Sweep | `page_archetype.instrument_panel` |
| GNSS Sky Plot | `page_archetype.instrument_panel` |
| SSTV | `page_archetype.instrument_panel` |
| Walkie Talkie | `page_archetype.instrument_panel` |
| PC Link | `page_archetype.service_panel` |
| USB | `page_archetype.service_panel` |

---

## 11. Governance Notes

### 11.1 Adding a New Page

新增页面时，先问它属于哪个 archetype。  
如果现有 archetype 都不适合，先修改公共 contract，再允许第三方布局依赖它。

### 11.2 Adding a New Region

新增 region 前，先证明它不是某个现有 region 的私有实现细节。

### 11.3 Adding a New Component

新增 component 前，先证明它值得成为共享 vocabulary，而不是某个页面私有 widget。

---

## 12. Current Status

当前这份 inventory 是 layout 可替换体系的第一版公共契约。

它已经足够约束后续工作不再继续把“页面内部实现细节”误当成第三方可以依赖的接口。后续 schema renderer、官方 presentation pack 和第三方包开发，都必须围绕这里的 contract 演进。

但必须明确：

- 这里列出的是规范层 public contract
- 它不等于所有 archetype 都已经完成运行时 renderer 解耦

当前成熟度矩阵：

| Archetype | Contract Status | Runtime Migration Status |
| --- | --- | --- |
| `page_archetype.directory_browser` | defined | first declarative schema pilot active |
| `page_archetype.chat_list` | defined | partially aligned |
| `page_archetype.chat_conversation` | defined | first declarative schema batch active |
| `page_archetype.chat_compose` | defined | first declarative schema batch active |
| `page_archetype.map_focus` | defined | first declarative schema batch active |
| `page_archetype.instrument_panel` | defined | first declarative schema batch active |
| `page_archetype.service_panel` | defined | first declarative schema batch active |
| `page_archetype.menu_dashboard` | defined | first declarative schema batch active |
| `page_archetype.boot_splash` | defined | first declarative schema batch active |
| `page_archetype.watch_face` | defined | first declarative schema batch active |

因此解释规则是：

- `defined` 表示第三方可以稳定依赖该 ID 集合作为长期 contract
- `first declarative schema batch active` 表示该 archetype 已进入首批 runtime，但当前仍只开放受控的 scaffold geometry
- 文档中的 archetype 映射表是目标语义归属，不是“该页面已经完成 presentation 解耦”的声明
