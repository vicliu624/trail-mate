# LVGL UI 代码最佳实践：写得美观、读得舒服、改得不痛

## 0. 先定“读代码的顺序”

LVGL 最大的问题不是 API，而是**命令式 + 样式噪声**：
一堆 `set_style_*` 把“结构意图”淹没了。

所以要强制一个阅读顺序：

1. **layout**：对象树与布局约束（结构）
2. **styles**：视觉规则（皮肤）
3. **components**：刷新流程与业务（什么时候画什么）
4. **input**：旋钮/键盘/触摸的焦点流（交互）

> 让读者能像看 UI 原型那样看代码。

---

## 1. 每个页面必须有“线框图 + 树状结构图”

把你现在的做法制度化：

* `*_layout.cpp` 头部必须有 ASCII 线框图
* 同时给出 Tree view（对象树）

这样读者不用读一行 LVGL API 就知道 UI 长啥样。

**规则**：线框图描述“结构”，不要描述“颜色/圆角”等样式细节。

---

## 2. 分层拆文件：layout / styles / components / input

### 2.1 layout：只写结构，不写样式，不写业务

layout 允许出现的 API：

* `lv_obj_create/lv_btn_create/lv_label_create`
* `lv_obj_set_size`
* `lv_obj_set_flex_flow / lv_obj_set_flex_grow / lv_obj_set_flex_align`
* `lv_obj_align / lv_obj_center`
* `lv_obj_clear_flag(SCROLLABLE) / set_scrollbar_mode`

layout **禁止**出现：

* `lv_obj_set_style_*`（全部禁止）
* `lv_obj_add_event_cb`（事件属于逻辑层）
* 数据格式化、分页、过滤（业务属于 components）

layout 提供的应是“结构函数”：

* `create_panels(...)`
* `ensure_subcontainers()`
* `create_list_item_struct(...)`

### 2.2 styles：集中定义 `lv_style_t`，提供 `apply_*`

styles 的规则：

* `init_once()` 幂等初始化，避免反复 `lv_style_init`
* **语义化样式名**：`btn_basic`, `btn_primary`, `panel_side`, `list_item`
* 状态样式用 `LV_STATE_*`（例如选中用 `LV_STATE_CHECKED`）

styles **禁止**：

* 业务逻辑
* 创建对象

你现在用 `apply_btn_filter()` + `LV_STATE_CHECKED` 来高亮，就是标准写法。

### 2.3 components：业务刷新与生命周期

components 负责：

* `refresh_ui()`：删旧建新/增量更新、分页、按钮 enable/disable、选中态刷新
* 事件回调（例如 Contacts/Nearby 切换）
* 数据格式化（format_time_status/format_snr）
* modal 的创建/清理

components 的规则：

* **禁止大段 style**（只调用 `style::apply_*`）
* **禁止创造新的布局结构**（结构只能通过 `layout::*`）

### 2.4 input：焦点与导航（rotary/keys）

input 负责：

* lv_group 聚焦管理（3 列之间怎么跳）
* rotary 的 rotate/press 映射（上下滚动/左右切换/按下触发）
* 不直接操纵样式，而是通过“选中态/焦点态”来驱动 UI 更新

input 的规则：

* 输入不直接改 UI 细节，而是改状态：`selected_index / focused_column / current_page`
* 然后调用 `refresh_ui()`（或更细粒度 update）

---

## 3. 把“样式噪声”彻底消灭的三条铁律

### 铁律 1：任何 `set_style_*` 都是“债务”

一旦你在某个页面里直接 `lv_obj_set_style_bg_color(...)`，
你很快会在 10 个页面复制 10 遍。

**正确做法**：变成 `apply_*()`，需要差异就通过 state。

### 铁律 2：状态通过 LV_STATE 表达，而不是改颜色表达

* 选中：`LV_STATE_CHECKED`
* 禁用：`LV_STATE_DISABLED`
* 聚焦：`LV_STATE_FOCUSED`

样式跟随状态变，逻辑层只改 state。

### 铁律 3：不要让 `refresh_ui()` 里出现“视觉细节”

`refresh_ui()` 应该像流程图：

* 选数据
* 算页码
* 创建 4 行
* 更新按钮状态
* 更新选中态

任何视觉细节都应是 `style::apply_*()`。

---

## 4. 统一“组件命名”和“对象树约定”

LVGL 代码的可读性，很大部分来自命名。

建议约定：

* panel：`filter_panel/list_panel/action_panel`
* container：`sub_container/bottom_container`
* item：`list_items[]`
* button：`*_btn`
* label：`*_label`

同时在 `contacts_state` 里明确哪些对象是“长期存在”，哪些是“refresh 重建”：

* 长期存在：panel、sub_container、bottom_container、pagination buttons
* 重建：list_items（每次 refresh 删除重建）

---

## 5. 对刷新策略做“显性声明”

LVGL 经常出现“到底应该删了重建还是增量更新”的混乱。

建议你在 components 里写清楚策略（注释或文档都行）：

* 本页 list items：**重建**（简单可控）
* pagination 按钮：**只创建一次**（避免重复分配）
* action panel：取决于选择项（可以增量/重建）

这会让后续优化（性能、内存）有明确落点。

---

## 6. 把布局常量变成“页面规格”，而不是散落 magic number

比如你已经做得很好：

* `kFilterPanelWidth = 80`
* `kActionPanelWidth = 100`
* `kItemsPerPage = 4`

建议进一步：

* layout 常量放 layout 文件
* 业务常量放 components 文件
* 样式常量（颜色/圆角/边框）放 styles 文件

**别混在一起**。

---

## 7. 可选的进阶：做一个“UI DSL”级别的 helper（但别过度）

当页面变多，你会发现大量重复：

* create_button_with_label
* create_panel_column
* create_row_container

你可以做少量 helper，但要守住边界：

* helper 只做结构（layout helper）
* helper 不做样式（样式交给 apply）

---

# 最后一段：一份落地清单（你可以贴到 CONTRIBUTING）

**每个新页面必须满足：**

* [ ] `*_layout.cpp` 头部有 wireframe + tree view
* [ ] layout 文件不出现任何 `set_style_*`
* [ ] styles 文件有 `init_once()`，所有样式通过 `apply_*()`
* [ ] components 的 `refresh_ui()` 不出现视觉细节
* [ ] input 独立文件，输入只改状态，不直接改样式
* [ ] magic number 分三类：layout / style / logic 各归其位
